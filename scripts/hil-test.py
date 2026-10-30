#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Drives Whisker with virtual controllers and checks what it does.

Creates a fake 2026 Steam Controller (28DE:1302) through /dev/uhid and an Xbox-style pad
and a Stadia-style pad through /dev/uinput, runs its own Whisker instance with a private config and runtime
directory, and checks the log for opened devices, trigger presses and releases, chords,
button capture over the socket and through the Decky backend, hotplug and haptics.
The user's config is never written; the test only checks it's unchanged. It needs write
access to /dev/uhid and /dev/uinput, so usually root.

  sudo python3 scripts/hil-test.py --whisker build/whisker
  sudo python3 scripts/hil-test.py --whisker ~deck/Applications/Whisker-x86_64.AppImage \\
      --user deck --service --puck

--user runs Whisker as that user, with their PipeWire session, so recordings and haptics
are exercised too (the noise gate is set so nothing is ever typed). --service stops that
user's whisker service during the test and starts it again afterwards. --puck also
deauthorizes and reauthorizes a real Steam Controller puck (28DE:1304) over USB.
--no-uhid skips the fake Steam Controller, which Steam would also pick up if it's running.
"""
import argparse
import asyncio
import fcntl
import glob
import hashlib
import importlib.util
import os
import pwd
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import types

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# uhid (linux/uhid.h)
UHID_DESTROY, UHID_START, UHID_OUTPUT = 1, 2, 6
UHID_GET_REPORT, UHID_GET_REPORT_REPLY = 9, 10
UHID_CREATE2, UHID_INPUT2 = 11, 12
UHID_SET_REPORT, UHID_SET_REPORT_REPLY = 13, 14
UHID_DATA_MAX = 4096
UHID_EVENT_SIZE = 4 + 128 + 64 + 64 + 2 + 2 + 16 + UHID_DATA_MAX
BUS_USB = 0x03

# Vendor collection with a 64-byte feature report 1 (hid-steam's command channel), input
# reports 0x42 (53 bytes) and 0x45 (45 bytes) and output report 0x80 (9 bytes, rumble).
STEAM_CONTROLLER_RDESC = bytes([
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,
    0x85, 0x01, 0x95, 0x3F, 0x09, 0x01, 0xB1, 0x02,
    0x85, 0x42, 0x95, 0x35, 0x09, 0x02, 0x81, 0x02,
    0x85, 0x45, 0x95, 0x2D, 0x09, 0x03, 0x81, 0x02,
    0x85, 0x80, 0x95, 0x09, 0x09, 0x04, 0x91, 0x02,
    0xC0,
])
STEAM_GET_SERIAL = 0xAE
TRITON = {"A": 0, "R3": 5, "L3": 15, "L5": 18, "GRIP_R": 28, "GRIP_L": 29}

# uinput (linux/uinput.h)
UI_DEV_CREATE, UI_DEV_DESTROY = 0x5501, 0x5502
UI_DEV_SETUP, UI_ABS_SETUP = 0x405C5503, 0x401C5504
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045567
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN = {"A": 0x130, "B": 0x131, "X": 0x134, "Y": 0x133, "L1": 0x136, "R1": 0x137,
       "L3": 0x13D, "R3": 0x13E, "L4": 0x224, "R4": 0x225, "L5": 0x226, "R5": 0x227,
       "TRIGGER_HAPPY1": 0x2C0, "TRIGGER_HAPPY2": 0x2C1}

results = []


def record(name, ok, detail=""):
    status = {True: "PASS", False: "FAIL", None: "SKIP"}[ok]
    results.append((name, status, detail))
    print(f"[{status}] {name}{': ' + detail if detail else ''}", flush=True)


class FakeSteamController:
    """A 2026 Steam Controller over uhid, answering hid-steam's feature requests."""

    def __init__(self, product=0x1302):
        self.product = product
        self.outputs = []
        self.last_command = b"\0"
        self.fd = os.open("/dev/uhid", os.O_RDWR)
        name = b"Whisker HIL Steam Controller"
        ev = struct.pack(f"<I128s64s64sHHIIII{UHID_DATA_MAX}s", UHID_CREATE2, name, b"whisker-hil", b"",
                         len(STEAM_CONTROLLER_RDESC), BUS_USB, 0x28DE, product, 0x100, 0,
                         STEAM_CONTROLLER_RDESC)
        os.write(self.fd, ev)
        self.alive = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _reply_for(self, cmd):
        if cmd[0] == STEAM_GET_SERIAL:
            return bytes([STEAM_GET_SERIAL, 0x15, 0x01]) + b"WHISKERHIL"
        return bytes([cmd[0], 2, 0, 0])

    def _serve(self):
        while self.alive:
            try:
                ev = os.read(self.fd, UHID_EVENT_SIZE)
            except OSError:
                return
            kind = struct.unpack_from("<I", ev)[0]
            if kind == UHID_OUTPUT:
                size = struct.unpack_from("<H", ev, 4 + UHID_DATA_MAX)[0]
                self.outputs.append((time.monotonic(), bytes(ev[4:4 + size])))
            elif kind == UHID_SET_REPORT:
                req_id, _, _, size = struct.unpack_from("<IBBH", ev, 4)
                data = ev[12:12 + size]
                self.last_command = bytes(data[1:]) if data and data[0] == 1 else bytes(data)
                os.write(self.fd, struct.pack("<IIH", UHID_SET_REPORT_REPLY, req_id, 0).ljust(UHID_EVENT_SIZE, b"\0"))
            elif kind == UHID_GET_REPORT:
                req_id, rnum, _ = struct.unpack_from("<IBB", ev, 4)
                data = (bytes([rnum]) + self._reply_for(self.last_command or b"\0")).ljust(64, b"\0")
                os.write(self.fd, struct.pack(f"<IIHH{UHID_DATA_MAX}s", UHID_GET_REPORT_REPLY, req_id, 0,
                                              len(data), data))

    def send(self, bits=()):
        word = sum(1 << b for b in bits)
        report = bytes([0x42, 0]) + word.to_bytes(4, "little")
        report = report.ljust(54, b"\0")
        os.write(self.fd, struct.pack(f"<IH{UHID_DATA_MAX}s", UHID_INPUT2, len(report), report))

    def rumble_reports(self, since):
        return [d for t, d in self.outputs if t >= since and d[:1] == b"\x80"]

    def destroy(self):
        self.alive = False
        try:
            os.write(self.fd, struct.pack("<I", UHID_DESTROY).ljust(UHID_EVENT_SIZE, b"\0"))
        finally:
            os.close(self.fd)


class FakePad:
    """A gamepad over uinput with every code in BTN, as the given vendor."""

    def __init__(self, vendor, product, name):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
        for code in BTN.values():
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
        fcntl.ioctl(self.fd, UI_SET_ABSBIT, 0)
        fcntl.ioctl(self.fd, UI_ABS_SETUP, struct.pack("<HHiiiiii", 0, 0, 0, -32768, 32767, 16, 128, 0))
        setup = struct.pack("<HHHH80sI", BUS_USB, vendor, product, 1, name.encode(), 0)
        fcntl.ioctl(self.fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)

    def key(self, name, value):
        for typ, code, val in ((EV_KEY, BTN[name], value), (EV_SYN, 0, 0)):
            os.write(self.fd, struct.pack("llHHi", 0, 0, typ, code, val))

    def destroy(self):
        fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        os.close(self.fd)


def xbox_pad():
    return FakePad(0x045E, 0x0B12, "Whisker HIL Xbox pad")


def stadia_pad():
    return FakePad(0x18D1, 0x9400, "Whisker HIL Stadia pad")


class Whisker:
    """A private Whisker instance whose log lines are collected for assertions."""

    def __init__(self, command, user, workdir, trigger):
        self.workdir = workdir
        self.lines = []
        config = os.path.join(workdir, "config.yaml")
        model = user_model(user) or f"{workdir}/no-model.gguf"
        with open(config, "w") as f:
            f.write(f"model: {model}\ntrigger_button: {trigger}\ntrigger_key: \"\"\n"
                    "overlay: false\ntray: false\naudio_cue: false\nhaptic: true\nhistory: 0\n"
                    "noise_threshold: 1.0\nrecording_timeout: 30\n"
                    "pedals:\n  left:\n    action: none\n  center:\n    action: none\n"
                    "    output: clipboard\n  right:\n    action: none\n")
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "WHISKER_SERVICE": "1",
               "APPIMAGE_EXTRACT_AND_RUN": "1", "HOME": workdir}
        preexec = None
        if user:
            pw = pwd.getpwnam(user)
            env["XDG_RUNTIME_DIR"] = f"/run/user/{pw.pw_uid}"
            env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path=/run/user/{pw.pw_uid}/bus"
            os.chown(workdir, pw.pw_uid, pw.pw_gid)
            os.chown(config, pw.pw_uid, pw.pw_gid)

            def preexec():
                os.initgroups(user, pw.pw_gid)
                os.setgid(pw.pw_gid)
                os.setuid(pw.pw_uid)
        else:
            env["XDG_RUNTIME_DIR"] = workdir
        self.socket_path = os.path.join(env["XDG_RUNTIME_DIR"], "whisker.sock")
        self.proc = subprocess.Popen(command + ["-c", config], env=env, cwd=workdir, preexec_fn=preexec,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._collect, daemon=True).start()

    def _collect(self):
        for line in self.proc.stdout:
            self.lines.append((time.monotonic(), line.rstrip()))
            print(f"    | {line.rstrip()}", flush=True)

    def mark(self):
        return len(self.lines)

    def wait(self, pattern, since=0, timeout=5.0):
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for _, line in self.lines[since:]:
                if rx.search(line):
                    return line
            time.sleep(0.05)
        return None

    def seen(self, pattern, since=0):
        rx = re.compile(pattern)
        return [line for _, line in self.lines[since:] if rx.search(line)]

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        return self.proc.returncode

    def kill_leftovers(self):
        """Kills processes started by this instance that outlived stop(); returns their pids."""
        marker = f"HOME={self.workdir}".encode()
        left = []
        for entry in os.listdir("/proc"):
            if not entry.isdigit() or int(entry) == os.getpid():
                continue
            try:
                with open(f"/proc/{entry}/environ", "rb") as f:
                    if marker in f.read().split(b"\0"):
                        os.kill(int(entry), signal.SIGKILL)
                        left.append(int(entry))
            except OSError:
                continue
        return left


def capture_over_socket(path, press, timeout=12):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(path)
        s.sendall(b"capture\n")
        threading.Timer(0.7, press).start()
        return s.makefile().readline().strip()


def capture_through_decky(path, press):
    sys.modules.setdefault("decky", types.SimpleNamespace(logger=types.SimpleNamespace(
        error=print, info=print), DECKY_USER_HOME=tempfile.gettempdir()))
    spec = importlib.util.spec_from_file_location("whisker_decky_main", os.path.join(REPO, "decky", "main.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module._socket_path = lambda: path
    threading.Timer(0.7, press).start()
    return asyncio.run(module.Plugin().capture_button())


def wait_trigger(whisker, edge, path_hint, since, timeout=3.0):
    return whisker.wait(rf"controller: trigger {edge} on {re.escape(path_hint)}$", since, timeout)


def device_path(whisker, name_pattern, since=0, timeout=8.0):
    line = whisker.wait(rf"controller: {name_pattern} streaming on (\S+)|controller: opened {name_pattern} at (\S+)",
                        since, timeout)
    if not line:
        return None
    return re.search(r"(/dev/\S+)", line).group(1)


def tap(press, release, hold=0.4):
    press()
    time.sleep(hold)
    release()


def test_steam_controller(whisker, fake, args):
    since = whisker.mark()
    fake.send()
    path = device_path(whisker, r"Steam Controller \(USB\)", 0)
    record("uhid: fake 2026 Steam Controller opened and streaming", path is not None, path or "no hidraw node")
    if not path:
        return None

    since = whisker.mark()
    for i in range(20):
        fake.send([TRITON["GRIP_L"]] if i % 2 else [TRITON["GRIP_R"], TRITON["GRIP_L"]])
        time.sleep(0.02)
    fake.send()
    time.sleep(0.3)
    record("uhid: grip touch bits never trigger", not whisker.seen("controller: trigger", since))

    since = whisker.mark()
    start = time.monotonic()
    fake.send([TRITON["L5"], TRITON["GRIP_L"]])
    down = wait_trigger(whisker, "down", path, since)
    recording = whisker.wait(r"whisker: recording\.\.\.", since, 2.0) if down else None
    time.sleep(0.5)
    fake.send([TRITON["GRIP_L"]])
    up = wait_trigger(whisker, "up", path, since)
    record("uhid: L5 press starts the trigger", down is not None)
    record("uhid: L5 release stops the trigger", up is not None)
    if recording:
        stopped = whisker.wait(r"whisker: (captured|below noise threshold)", since, 5.0)
        record("uhid: recording started and stopped", stopped is not None)
        time.sleep(0.5)
        pulses = whisker.seen(rf"haptic: \d+ ms pulse on {re.escape(path)}", since)
        rumble = fake.rumble_reports(start)
        stops = [r for r in rumble if not any(r[1:])]
        record("uhid: haptics on the triggering controller", bool(pulses) and len(rumble) > len(stops) and bool(stops),
               f"{len(pulses)} pulses logged, {len(rumble)} rumble reports, {len(stops)} stops")
    else:
        record("uhid: recording started and stopped", None, "no transcription in this environment")
        record("uhid: haptics on the triggering controller", None, "needs a recording")
    return path


def test_steam_controller_hotplug(whisker, fake, path, args):
    since = whisker.mark()
    fake.send([TRITON["L5"]])
    down = wait_trigger(whisker, "down", path, since)
    fake.destroy()
    up = wait_trigger(whisker, "up", path, since)
    gone = whisker.wait(r"controller: .* at .* disconnected", since)
    record("uhid: unplug mid-hold releases the trigger", bool(down and up and gone))
    since = whisker.mark()
    fake = FakeSteamController()
    time.sleep(0.5)
    fake.send()
    again = device_path(whisker, r"Steam Controller \(USB\)", since)
    record("uhid: replugged controller is reopened", again is not None, again or "")
    return fake


def test_xbox_pad(whisker, pad, hotplug=True):
    path = device_path(whisker, r"Whisker HIL Xbox pad", 0)
    record("uinput: Xbox-style pad opened", path is not None, path or "")
    if not path:
        return pad
    since = whisker.mark()
    pad.key("L5", 1)
    down = wait_trigger(whisker, "down", path, since)
    time.sleep(0.3)
    pad.key("L5", 0)
    up = wait_trigger(whisker, "up", path, since)
    record("uinput: BTN_GRIPL2 (L5) press and release", bool(down and up))
    if not hotplug:
        return pad
    since = whisker.mark()
    pad.key("L5", 1)
    down = wait_trigger(whisker, "down", path, since)
    pad.destroy()
    up = wait_trigger(whisker, "up", path, since)
    record("uinput: unplug mid-hold releases the trigger", bool(down and up))
    since = whisker.mark()
    pad = xbox_pad()
    record("uinput: replugged pad is reopened", device_path(whisker, r"Whisker HIL Xbox pad", since) is not None)
    return pad


def test_capture(whisker, press, release, label):
    def gesture():
        tap(press, release)
    try:
        reply = capture_over_socket(whisker.socket_path, gesture)
    except OSError as e:
        reply = f"error {e}"
    record(f"capture over the socket ({label})", reply.startswith("L5 "), repr(reply))
    time.sleep(0.5)
    try:
        result = capture_through_decky(whisker.socket_path, gesture)
    except Exception as e:  # noqa: BLE001 - reported as a failed check
        result = {"error": repr(e)}
    record(f"capture through the Decky backend ({label})", result.get("button") == "L5", repr(result))


def test_chords(whisker, pad, fake):
    if pad:
        path = device_path(whisker, r"Whisker HIL Xbox pad", 0)
        since = whisker.mark()
        pad.key("L3", 1)
        time.sleep(0.3)
        early = whisker.seen("controller: trigger", since)
        pad.key("R3", 1)
        down = wait_trigger(whisker, "down", path, since)
        pad.key("R3", 0)
        up = wait_trigger(whisker, "up", path, since)
        pad.key("L3", 0)
        record("uinput: L3+R3 chord fires only with both held", bool(not early and down and up))
    if fake:
        path = device_path(whisker, r"Steam Controller \(USB\)", 0)
        since = whisker.mark()
        fake.send([TRITON["L3"]])
        time.sleep(0.3)
        early = whisker.seen("controller: trigger", since)
        fake.send([TRITON["L3"], TRITON["R3"], TRITON["GRIP_L"]])
        down = wait_trigger(whisker, "down", path, since)
        fake.send([TRITON["R3"]])
        up = wait_trigger(whisker, "up", path, since)
        fake.send()
        record("uhid: L3+R3 chord fires only with both held", bool(not early and down and up))


def test_stadia(whisker, stadia, pad):
    path = device_path(whisker, r"Whisker HIL Stadia pad", 0)
    record("uinput: Stadia-style pad opened", path is not None, path or "")
    if not path:
        return
    since = whisker.mark()
    stadia.key("TRIGGER_HAPPY2", 1)
    down = wait_trigger(whisker, "down", path, since)
    time.sleep(0.3)
    stadia.key("TRIGGER_HAPPY2", 0)
    up = wait_trigger(whisker, "up", path, since)
    record("uinput: Stadia BTN_TRIGGER_HAPPY2 (capture) press and release", bool(down and up))
    if pad:
        since = whisker.mark()
        tap(lambda: pad.key("TRIGGER_HAPPY2", 1), lambda: pad.key("TRIGGER_HAPPY2", 0))
        time.sleep(0.5)
        ignored = not whisker.seen("controller: trigger", since)
        record("uinput: BTN_TRIGGER_HAPPY2 on a non-Google pad is ignored", ignored)
    try:
        reply = capture_over_socket(whisker.socket_path, lambda: tap(lambda: stadia.key("TRIGGER_HAPPY1", 1),
                                                                    lambda: stadia.key("TRIGGER_HAPPY1", 0)))
    except OSError as e:
        reply = f"error {e}"
    record("capture over the socket (Stadia assistant)", reply.startswith("assistant "), repr(reply))


def find_puck():
    for dev in glob.glob("/sys/bus/usb/devices/*"):
        try:
            with open(f"{dev}/idVendor") as v, open(f"{dev}/idProduct") as p:
                if v.read().strip() == "28de" and p.read().strip() == "1304":
                    return dev
        except OSError:
            continue
    return None


def test_puck(whisker):
    dev = find_puck()
    if not dev:
        record("puck: deauthorize and reauthorize", None, "no 28DE:1304 puck connected")
        return
    since = whisker.mark()
    with open(f"{dev}/authorized", "w") as f:
        f.write("0")
    gone = whisker.wait(r"controller: Steam Controller \(puck\) at .* disconnected", since, 5.0)
    time.sleep(1)
    since_back = whisker.mark()
    with open(f"{dev}/authorized", "w") as f:
        f.write("1")
    back = whisker.wait(r"controller: opened Steam Controller \(puck\)", since_back, 10.0)
    time.sleep(2)
    slots = len(whisker.seen(r"controller: opened Steam Controller \(puck\)", since_back))
    record("puck: slots closed and reopened after USB reauthorization", bool(gone and back), f"{slots} slots reopened")


def user_systemctl(user, *args):
    return subprocess.run(["systemctl", f"--machine={user}@", "--user", *args],
                          capture_output=True, text=True)


def user_config_path(user):
    home = pwd.getpwnam(user).pw_dir if user else os.path.expanduser("~")
    return os.path.join(home, ".config", "pedal-stt", "config.yaml")


def user_model(user):
    """The user's model, so recordings (and the haptics around them) run for real."""
    if not user:
        return None
    try:
        with open(user_config_path(user)) as f:
            for line in f:
                if line.startswith("model:"):
                    model = line.partition(":")[2].strip().strip('"')
                    return model.replace("~", pwd.getpwnam(user).pw_dir, 1) if model.startswith("~") else model
    except FileNotFoundError:
        pass
    return None


def config_digest(user):
    path = user_config_path(user)
    try:
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()
    except FileNotFoundError:
        return None


def coredumps_since(start):
    if not subprocess.run(["sh", "-c", "command -v coredumpctl"], capture_output=True).returncode == 0:
        return None
    out = subprocess.run(["coredumpctl", "--no-pager", "--no-legend", "list", f"--since=@{int(start)}"],
                         capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if "whisker" in line]


def run_phase(args, workdir, trigger, body):
    whisker = Whisker(args.whisker, args.user, workdir, trigger)
    try:
        if not whisker.wait(r"whisker: ready", timeout=90):
            record(f"whisker starts with trigger {trigger}", False, "no 'whisker: ready'")
            return
        record(f"whisker starts with trigger {trigger}", True)
        body(whisker)
        record(f"whisker still running after the {trigger} checks", whisker.proc.poll() is None)
    finally:
        code = whisker.stop()
        record(f"whisker exits cleanly ({trigger})", code == 0, f"exit code {code}")
        left = whisker.kill_leftovers()
        record(f"no whisker process left alive ({trigger})", not left, f"killed pids {left}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--whisker", required=True, help="command that runs whisker (binary or AppImage)")
    parser.add_argument("--user", help="run Whisker as this user, with their audio session")
    parser.add_argument("--service", action="store_true", help="stop --user's whisker service during the test")
    parser.add_argument("--puck", action="store_true", help="also reauthorize a real 28DE:1304 puck")
    parser.add_argument("--no-uhid", action="store_true", help="skip the fake Steam Controller")
    args = parser.parse_args()
    command = args.whisker.split()
    if os.path.exists(command[0]):
        command[0] = os.path.abspath(command[0])
    args.whisker = command
    if (args.user or args.service or args.puck) and os.geteuid() != 0:
        sys.exit("hil-test: --user, --service and --puck need root")

    start = time.time()
    digest = config_digest(args.user)
    restart_service = False
    if args.service and args.user:
        restart_service = user_systemctl(args.user, "is-active", "--quiet", "whisker").returncode == 0
        user_systemctl(args.user, "stop", "whisker")

    fake = pad = stadia = None
    try:
        if not args.no_uhid:
            try:
                fake = FakeSteamController()
            except OSError as e:
                record("uhid: create fake 2026 Steam Controller", False, str(e))
        try:
            pad = xbox_pad()
        except OSError as e:
            record("uinput: create Xbox-style pad", False, str(e))
        try:
            stadia = stadia_pad()
        except OSError as e:
            record("uinput: create Stadia-style pad", False, str(e))
        time.sleep(1)

        with tempfile.TemporaryDirectory(prefix="whisker-hil-") as workdir:
            def single_button(whisker):
                nonlocal fake, pad
                path = test_steam_controller(whisker, fake, args) if fake else None
                if fake and path:
                    test_capture(whisker, lambda: fake.send([TRITON["L5"]]), lambda: fake.send(), "Steam Controller")
                    fake = test_steam_controller_hotplug(whisker, fake, path, args)
                if pad:
                    pad = test_xbox_pad(whisker, pad)
                    test_capture(whisker, lambda: pad.key("L5", 1), lambda: pad.key("L5", 0), "Xbox pad")
                if args.puck:
                    test_puck(whisker)

            run_phase(args, workdir, "L5", single_button)
            run_phase(args, workdir, "L3+R3", lambda whisker: test_chords(whisker, pad, fake))
            if stadia:
                run_phase(args, workdir, "capture", lambda whisker: test_stadia(whisker, stadia, pad))
    finally:
        for dev in (fake, pad, stadia):
            if dev:
                dev.destroy()
        if restart_service:
            user_systemctl(args.user, "start", "whisker")
            time.sleep(3)
            record("whisker service active again", user_systemctl(args.user, "is-active", "--quiet", "whisker")
                   .returncode == 0)
        record("user config unchanged", config_digest(args.user) == digest)
        dumps = coredumps_since(start)
        record("no new whisker core dumps", None if dumps is None else not dumps,
               "coredumpctl missing" if dumps is None else "; ".join(dumps))

    print()
    width = max(len(name) for name, _, _ in results)
    for name, status, detail in results:
        print(f"{status:4}  {name.ljust(width)}  {detail}")
    failed = [r for r in results if r[1] == "FAIL"]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks without failure")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
