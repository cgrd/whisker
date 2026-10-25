#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prints which report bits change when you press controller buttons.

Watches every Valve (28DE) hidraw node and the key events of every evdev gamepad.
Keep your hands off the controllers during calibration so sticks, IMU, sequence
numbers and touch sensors get masked, then press one button at a time.

  python3 scripts/controller-capture.py          # Valve hidraw nodes + evdev gamepads
  python3 scripts/controller-capture.py --all    # every hidraw node
"""
import fcntl
import glob
import os
import select
import struct
import sys
import time

CALIBRATE_S = 3.0
VALVE = 0x28DE
EV_KEY = 0x01
BTN_SOUTH = 0x130
EVENT_FMT = "llHHi"
EVENT_SIZE = struct.calcsize(EVENT_FMT)


def ioc_read(nr, size):
    return (2 << 30) | (size << 16) | (ord("E") << 8) | nr


def evdev_name(fd):
    return fcntl.ioctl(fd, ioc_read(0x06, 256), bytes(256)).split(b"\0")[0].decode(errors="replace")


def has_key(fd, code):
    bits = fcntl.ioctl(fd, ioc_read(0x20 + EV_KEY, 96), bytes(96))
    return bool(bits[code // 8] >> (code % 8) & 1)


def uevent(path):
    try:
        with open(path) as f:
            return dict(line.strip().partition("=")[::2] for line in f)
    except OSError:
        return {}


def interface_number(sys_node):
    dev = os.path.realpath(os.path.join(sys_node, "device"))
    while dev != "/":
        path = os.path.join(dev, "bInterfaceNumber")
        if os.path.exists(path):
            with open(path) as f:
                return f.read().strip()
        dev = os.path.dirname(dev)
    return "-"


def node_number(path):
    return int("".join(c for c in os.path.basename(path) if c.isdigit()) or 0)


def open_hidraw(show_all):
    nodes = {}
    for sys_node in sorted(glob.glob("/sys/class/hidraw/hidraw*"), key=node_number):
        ue = uevent(os.path.join(sys_node, "device/uevent"))
        bus, vid, pid = (ue.get("HID_ID", "0:0:0").split(":") + ["0", "0"])[:3]
        vid, pid = int(vid, 16) & 0xFFFF, int(pid, 16) & 0xFFFF
        node = "/dev/" + os.path.basename(sys_node)
        print(f"  {node} {vid:04x}:{pid:04x} bus={bus} iface={interface_number(sys_node)} '{ue.get('HID_NAME', '?')}'")
        if vid != VALVE and not show_all:
            continue
        try:
            nodes[os.open(node, os.O_RDONLY | os.O_NONBLOCK)] = node
        except OSError as e:
            print(f"    cannot open: {e.strerror}")
    return nodes


def open_evdev():
    pads = {}
    for node in sorted(glob.glob("/dev/input/event*"), key=node_number):
        try:
            fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        if not has_key(fd, BTN_SOUTH):
            os.close(fd)
            continue
        product = uevent(f"/sys/class/input/{os.path.basename(node)}/device/uevent").get("PRODUCT", "?")
        print(f"  {node} '{evdev_name(fd)}' PRODUCT={product}")
        pads[fd] = node
    return pads


def changed_bits(old, new, noisy):
    diff = int.from_bytes(old, "little") ^ int.from_bytes(new, "little")
    diff &= ~noisy
    value = int.from_bytes(new, "little")
    return [f"byte{b // 8}.bit{b % 8}(bit{b})={value >> b & 1}" for b in range(len(new) * 8) if diff >> b & 1]


def read_evdev(fd, node, calibrating):
    data = os.read(fd, EVENT_SIZE * 64)
    for off in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
        _, _, typ, code, value = struct.unpack_from(EVENT_FMT, data, off)
        if typ == EV_KEY and not calibrating:
            print(f"{node} EV_KEY code=0x{code:03x} value={value}", flush=True)


def main():
    print(f"kernel {os.uname().release}")
    print("hidraw nodes:")
    hidraw = open_hidraw("--all" in sys.argv)
    print("evdev gamepads:")
    evdev = open_evdev()

    last, noisy, sizes = {}, {}, {}
    start = time.monotonic()
    calibrating = True
    print(f"calibrating for {CALIBRATE_S:.0f}s, don't touch anything...", flush=True)
    fds = list(hidraw) + list(evdev)
    while fds:
        ready, _, _ = select.select(fds, [], [], 0.5)
        if calibrating and time.monotonic() - start > CALIBRATE_S:
            calibrating = False
            for (node, rid), size in sorted(sizes.items()):
                mask = noisy.get((node, rid), 0)
                print(f"  {node} report 0x{rid:02x} len={size} masked bits={bin(mask).count('1')}")
            print("ready: press one button at a time, ctrl-c to quit", flush=True)
        for fd in ready:
            try:
                if fd in evdev:
                    read_evdev(fd, evdev[fd], calibrating)
                    continue
                data = os.read(fd, 128)
            except OSError as e:
                print(f"{hidraw.get(fd) or evdev.get(fd)} read error: {e.strerror}")
                fds.remove(fd)
                continue
            if not data:
                continue
            key = (hidraw[fd], data[0])
            sizes[key] = len(data)
            old = last.get(key)
            last[key] = data
            if old is None:
                print(f"{hidraw[fd]} first report 0x{data[0]:02x} len={len(data)}: {data.hex()}", flush=True)
                continue
            if len(old) != len(data):
                continue
            if calibrating:
                noisy[key] = noisy.get(key, 0) | (int.from_bytes(old, "little") ^ int.from_bytes(data, "little"))
                continue
            bits = changed_bits(old, data, noisy.get(key, 0))
            if bits:
                print(f"{hidraw[fd]} report 0x{data[0]:02x} {' '.join(bits)}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
