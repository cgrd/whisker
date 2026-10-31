# SPDX-License-Identifier: MIT
import asyncio
import hashlib
import json
import os
from pathlib import Path

import decky

HOME = Path(getattr(decky, "DECKY_USER_HOME", None) or Path.home())
CONFIG_PATH = HOME / ".config" / "pedal-stt" / "config.yaml"
MODELS_DIR = HOME / ".local" / "share" / "pedal-stt" / "models"
PLUGIN_DIR = Path(__file__).resolve().parent


def _load_model_table() -> dict:
    # Packaged next to main.py; in a source checkout it's the repo's models.json
    for path in (PLUGIN_DIR / "models.json", PLUGIN_DIR.parent / "models.json"):
        if path.exists():
            return json.loads(path.read_text())
    raise FileNotFoundError("models.json not found")


MODEL_TABLE = _load_model_table()


def megabytes(size: int) -> int:
    return (size + 500_000) // 1_000_000


def model_label(model: dict) -> str:
    return f"{MODEL_TABLE['name']}, {model['bits']}-bit ({model['quant']}), {megabytes(model['size'])} MB"


def model_url(model: dict) -> str:
    # Pinned to the commit the hashes in models.json were taken from
    return f"{MODEL_TABLE['repo']}/resolve/{MODEL_TABLE['revision']}/{model['file']}"


def _plugin_version() -> str:
    return json.loads((PLUGIN_DIR / "package.json").read_text())["version"]


USER_AGENT = f"Whisker/{_plugin_version()} (+https://github.com/cgrd/whisker)"


def curl_download_args(url: str, dest: Path) -> list[str]:
    # One retry on a transient error; a failed download stops until the user starts it again
    return ["curl", "-fsSL", "--retry", "1", "-A", USER_AGENT, "-o", str(dest), url]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def install_verified(tmp: Path, dest: Path, size: int, sha256: str) -> str:
    """Moves a finished download into place if its size and hash match, else deletes it. Returns an error or ""."""
    if not tmp.is_file():
        error = "The download is missing"
    elif tmp.stat().st_size != size:
        error = f"The download has the wrong size ({tmp.stat().st_size} bytes, expected {size})"
    elif _sha256(tmp) != sha256:
        error = "The download is corrupt (SHA-256 mismatch)"
    else:
        tmp.rename(dest)
        return ""
    tmp.unlink(missing_ok=True)
    return error


def known_model(file: str) -> dict:
    for model in MODEL_TABLE["models"]:
        if model["file"] == file:
            return model
    raise ValueError(f"unknown model: {file!r}")


def _part_path(file: str) -> Path:
    return MODELS_DIR / f".{file}.part"


PEDAL_NAMES = ("left", "center", "right")
SUBMIT_KEYS = ("none", "enter", "tab")
BUTTONS = (
    "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "L3", "R3", "L4", "L5", "R4", "R5",
    "select", "start", "guide", "assistant", "capture", "up", "down", "left", "right",
)
CAPTURE_TIMEOUT_S = 12
# Must match builtinProfiles() in src/chat_profile.cpp
BUILTIN_CHAT_PROFILES = ("wow",)
CHAT_PROFILE_MODES = ("auto", "none", "")


def button_chord(path: str, value):
    if value == "":
        return value
    parts = [p.strip() for p in value.split("+")] if isinstance(value, str) else []
    if not parts or any(p not in BUTTONS for p in parts) or len(set(parts)) != len(parts):
        raise ValueError(f"{path}: '{value}' is not a button or chord like L3+R3")
    return "+".join(parts)


def parse_capture_reply(line: str) -> dict:
    line = line.strip()
    if line == "busy":
        return {"error": "Whisker is recording, try again"}
    chord, _, device = line.partition(" ")
    if not chord:
        return {"error": "No button pressed"}
    return {"button": button_chord("capture", chord), "device": device}

def chat_profile_names(config: dict) -> list[str]:
    custom = config.get("chat_profiles")
    custom = list(custom) if isinstance(custom, dict) else []
    return [*BUILTIN_CHAT_PROFILES,
            *(name for name in custom if name not in (*BUILTIN_CHAT_PROFILES, *CHAT_PROFILE_MODES))]


TOP_OPTIONS = {
    "model": str,
    "backend": ("vulkan", "cpu"),
    "audio_device": str,
    "trigger_key": str,
    "trigger_button": button_chord,
    "trigger_mode": ("hold", "toggle"),
    "overlay_position": ("bottom", "top", "bottom-left", "bottom-right", "top-left", "top-right"),
    "submit_key": SUBMIT_KEYS,
    "chat_profile": str,
    "history": int,
    "noise_threshold": float,
    "overlay": bool,
    "tray": bool,
    "audio_cue": bool,
    "haptic": bool,
    "dictation_commands": bool,
    "auto_capitalize": bool,
    "undo_enabled": bool,
    "recording_timeout": int,
    "auto_update": bool,
}

# exec actions and their commands can only be set by editing config.yaml, so nothing
# that can talk to the plugin can make the daemon run a program
PEDAL_OPTIONS = {
    "action": ("none", "key", "transcribe"),
    "key": str,
    "mode": ("tap", "hold"),
    "output": ("type", "clipboard", "both"),
    "suffix": SUBMIT_KEYS,
}


def parse_yaml(text: str) -> dict:
    root = {}
    stack = [(-1, root)]
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or ":" not in stripped:
            continue
        indent = len(line) - len(line.lstrip())
        while indent <= stack[-1][0]:
            stack.pop()
        key, _, value = stripped.partition(":")
        key, value = key.strip(), value.strip()
        parent = stack[-1][1]
        if value:
            parent[key] = _parse_value(value)
        else:
            parent[key] = {}
            stack.append((indent, parent[key]))
    return root


def _parse_value(value: str):
    if value in ("true", "True"):
        return True
    if value in ("false", "False"):
        return False
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        pass
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1]
    return value


def serialize_yaml(data: dict, depth: int = 0) -> str:
    pad = "  " * depth
    out = ""
    for key, value in data.items():
        if isinstance(value, dict):
            out += f"{pad}{key}:\n" + serialize_yaml(value, depth + 1)
        else:
            out += f"{pad}{key}: {_serialize_value(value)}\n"
    return out


def _serialize_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        if not value or any(c in value for c in ' :#"'):
            return f'"{value}"'
        return value
    return str(value)


def _read_config() -> dict:
    if not CONFIG_PATH.exists():
        return {}
    return parse_yaml(CONFIG_PATH.read_text())


def _write_config(config: dict) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(serialize_yaml(config))


def _option_spec(parts: list[str]):
    if len(parts) == 1 and parts[0] in TOP_OPTIONS:
        return TOP_OPTIONS[parts[0]]
    if len(parts) == 3 and parts[0] == "pedals" and parts[1] in PEDAL_NAMES and parts[2] in PEDAL_OPTIONS:
        return PEDAL_OPTIONS[parts[2]]
    raise ValueError(f"unknown option '{'.'.join(parts)}'")


def _coerce(path: str, spec, value):
    if spec is button_chord:
        return button_chord(path, value)
    if isinstance(spec, tuple):
        if value not in spec:
            raise ValueError(f"{path}: '{value}' not in {spec}")
        return value
    if spec is bool and isinstance(value, bool):
        return value
    if spec in (int, float) and isinstance(value, (int, float)) and not isinstance(value, bool):
        return round(float(value), 6) if spec is float else int(round(value))
    if spec is str and isinstance(value, str):
        return value
    raise ValueError(f"{path}: expected {spec.__name__}, got {value!r}")


def _runtime_dir() -> str:
    return f"/run/user/{os.getuid()}"


def _socket_path() -> str:
    return f"{_runtime_dir()}/whisker.sock"


def _user_env() -> dict:
    # Decky's environment lacks the user session variables that systemctl --user needs
    runtime = _runtime_dir()
    return {
        **os.environ,
        "HOME": str(HOME),
        "XDG_RUNTIME_DIR": runtime,
        "DBUS_SESSION_BUS_ADDRESS": f"unix:path={runtime}/bus",
    }


async def _run(cmd: list[str]) -> tuple[int, str]:
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=_user_env(),
    )
    stdout, stderr = await proc.communicate()
    output = stdout.decode().strip() if stdout else stderr.decode().strip()
    return proc.returncode, output


class Plugin:
    _download_error = ""

    async def get_status(self) -> dict:
        rc_active, _ = await _run(["systemctl", "--user", "is-active", "whisker"])
        rc_enabled, _ = await _run(["systemctl", "--user", "is-enabled", "whisker"])
        return {
            "running": rc_active == 0,
            "enabled": rc_enabled == 0,
        }

    async def start_service(self) -> None:
        await _run(["systemctl", "--user", "start", "whisker"])

    async def stop_service(self) -> None:
        await _run(["systemctl", "--user", "stop", "whisker"])

    async def enable_service(self) -> None:
        await _run(["systemctl", "--user", "enable", "whisker"])

    async def disable_service(self) -> None:
        await _run(["systemctl", "--user", "disable", "whisker"])

    async def get_config(self) -> dict:
        return _read_config()

    async def set_option(self, path: str, value) -> None:
        parts = path.split(".")
        value = _coerce(path, _option_spec(parts), value)
        config = _read_config()
        # Profiles themselves are config-file only; the plugin just picks one
        if parts == ["chat_profile"] and value not in (*CHAT_PROFILE_MODES, *chat_profile_names(config)):
            raise ValueError(f"chat_profile: unknown profile '{value}'")
        node = config
        for part in parts[:-1]:
            if not isinstance(node.get(part), dict):
                node[part] = {}
            node = node[part]
        node[parts[-1]] = value
        _write_config(config)

    async def get_chat_profiles(self) -> list[str]:
        return chat_profile_names(_read_config())

    async def get_audio_sources(self) -> list[dict]:
        _, output = await _run(["pactl", "list", "sources"])
        sources = []
        name = ""
        for line in output.splitlines():
            line = line.strip()
            if line.startswith("Name:"):
                name = line[5:].strip()
            elif line.startswith("Description:") and name:
                if "monitor" not in name:
                    sources.append({"id": name, "name": line[12:].strip()})
                name = ""
        return sources

    async def get_models(self) -> list[str]:
        if not MODELS_DIR.exists():
            return []
        return sorted(str(p) for p in MODELS_DIR.glob("*.gguf"))

    async def get_known_models(self) -> dict:
        return {
            "repo": MODEL_TABLE["repo"],
            "original": MODEL_TABLE["original"],
            "recommended_reason": MODEL_TABLE["recommended_reason"],
            "models": [
                {
                    "file": m["file"],
                    "label": model_label(m),
                    "note": m["note"],
                    "recommended": m["file"] == MODEL_TABLE["recommended"],
                }
                for m in MODEL_TABLE["models"]
            ],
        }

    async def get_model_status(self) -> dict:
        configured = _read_config().get("model")
        configured = os.path.expanduser(configured) if isinstance(configured, str) else ""
        downloading = next((m for m in MODEL_TABLE["models"] if _part_path(m["file"]).exists()), None)
        return {
            "configured": configured,
            "configured_exists": bool(configured) and Path(configured).is_file(),
            "downloaded": [m["file"] for m in MODEL_TABLE["models"] if (MODELS_DIR / m["file"]).is_file()],
            "downloading": downloading["file"] if downloading else "",
            "progress_mb": megabytes(_part_path(downloading["file"]).stat().st_size) if downloading else 0,
            "total_mb": megabytes(downloading["size"]) if downloading else 0,
            "error": self._download_error,
        }

    async def download_model(self, file: str) -> bool:
        """Downloads the model the user picked. Nothing calls this without a click on a model."""
        model = known_model(file)
        dest = MODELS_DIR / model["file"]
        self._download_error = ""
        if dest.is_file():
            await self.set_option("model", str(dest))
            return True
        MODELS_DIR.mkdir(parents=True, exist_ok=True)
        asyncio.create_task(self._do_download(model, dest, _part_path(model["file"])))
        return True

    async def _do_download(self, model: dict, dest: Path, tmp: Path) -> None:
        proc = await asyncio.create_subprocess_exec(
            *curl_download_args(model_url(model), tmp),
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.PIPE,
            env=_user_env(),
        )
        _, stderr = await proc.communicate()
        if proc.returncode != 0:
            tmp.unlink(missing_ok=True)
            self._download_error = "Download failed, check the connection and try again"
            decky.logger.error(f"Model download failed: {stderr.decode()}")
            return
        error = await asyncio.to_thread(install_verified, tmp, dest, model["size"], model["sha256"])
        if error:
            self._download_error = f"{error}. Try again."
            decky.logger.error(f"Model download rejected: {error}")
            return
        await self.set_option("model", str(dest))
        decky.logger.info("Model download complete")

    async def cancel_download(self) -> None:
        for model in MODEL_TABLE["models"]:
            tmp = _part_path(model["file"])
            if tmp.exists():
                await _run(["pkill", "-f", f"curl.*{model['file']}"])
                tmp.unlink(missing_ok=True)

    async def start_recording(self) -> bool:
        return await self._send_command("start")

    async def stop_recording(self) -> bool:
        return await self._send_command("stop")

    async def capture_button(self) -> dict:
        try:
            reader, writer = await asyncio.open_unix_connection(_socket_path())
        except OSError:
            return {"error": "Whisker isn't running"}
        try:
            writer.write(b"capture\n")
            await writer.drain()
            line = await asyncio.wait_for(reader.readline(), CAPTURE_TIMEOUT_S)
        except (OSError, asyncio.TimeoutError) as e:
            decky.logger.error(f"Button capture failed: {e!r}")
            return {"error": "No reply from Whisker"}
        finally:
            writer.close()
        try:
            return parse_capture_reply(line.decode())
        except ValueError as e:
            decky.logger.error(str(e))
            return {"error": "Unknown button"}

    async def _send_command(self, cmd: str) -> bool:
        import socket
        sock_path = _socket_path()
        for attempt in range(2):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(sock_path)
                s.sendall(f"{cmd}\n".encode())
                s.close()
                return True
            except (ConnectionRefusedError, FileNotFoundError):
                if attempt == 0:
                    decky.logger.info("Socket unavailable, starting whisker service")
                    await _run(["systemctl", "--user", "start", "whisker"])
                    await asyncio.sleep(2)
                else:
                    decky.logger.error("Socket still unavailable after starting service")
                    return False
            except Exception as e:
                decky.logger.error(f"Command socket error: {e}")
                return False
        return False

    async def restart_service(self) -> None:
        await _run(["systemctl", "--user", "restart", "whisker"])

    async def get_logs(self, lines: int = 20) -> str:
        _, output = await _run([
            "journalctl", "--user", "-u", "whisker",
            "--no-pager", "-n", str(lines), "--output", "short",
        ])
        return output

    async def get_last_transcription(self) -> str:
        _, output = await _run([
            "journalctl", "--user", "-u", "whisker",
            "--no-pager", "-n", "50", "--output", "cat",
        ])
        for line in reversed(output.splitlines()):
            if line.startswith('whisker: "') and line.endswith('"'):
                return line[len('whisker: "'):-1]
        return ""

    async def get_diagnostics(self) -> dict:
        rc_active, _ = await _run(["systemctl", "--user", "is-active", "whisker"])
        running = rc_active == 0

        sock_path = _socket_path()
        sock_exists = os.path.exists(sock_path)

        model_path = ""
        config = await self.get_config()
        raw = config.get("model", "")
        if isinstance(raw, str):
            model_path = str(HOME) + raw[1:] if raw.startswith("~") else raw
        model_exists = os.path.isfile(model_path) if model_path else False

        _, mem = await _run(["systemctl", "--user", "show", "whisker", "-p", "MemoryCurrent"])
        mem_mb = 0
        if "=" in mem:
            try:
                mem_mb = int(mem.split("=")[1]) // (1024 * 1024)
            except (ValueError, IndexError):
                pass

        return {
            "running": running,
            "socket": sock_exists,
            "model_exists": model_exists,
            "model_path": model_path,
            "memory_mb": mem_mb,
        }

    async def _main(self) -> None:
        decky.logger.info("Whisker plugin loaded")

    async def _unload(self) -> None:
        decky.logger.info("Whisker plugin unloaded")

    async def _migration(self) -> None:
        pass
