# SPDX-License-Identifier: MIT
import asyncio
import os
from pathlib import Path

import decky

HOME = Path(getattr(decky, "DECKY_USER_HOME", None) or Path.home())
CONFIG_PATH = HOME / ".config" / "pedal-stt" / "config.yaml"
MODELS_DIR = HOME / ".local" / "share" / "pedal-stt" / "models"
DEFAULT_MODEL_URL = "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q8_0.gguf"
DEFAULT_MODEL_NAME = "parakeet-unified-en-0.6b-Q8_0.gguf"


PEDAL_NAMES = ("left", "center", "right")
SUBMIT_KEYS = ("none", "enter", "tab")

TOP_OPTIONS = {
    "model": str,
    "backend": ("vulkan", "cpu"),
    "audio_device": str,
    "trigger_key": str,
    "trigger_button": str,
    "trigger_mode": ("hold", "toggle"),
    "overlay_position": ("bottom", "top", "bottom-left", "bottom-right", "top-left", "top-right"),
    "submit_key": SUBMIT_KEYS,
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
        node = config
        for part in parts[:-1]:
            if not isinstance(node.get(part), dict):
                node[part] = {}
            node = node[part]
        node[parts[-1]] = value
        _write_config(config)

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

    async def get_model_status(self) -> dict:
        dest = MODELS_DIR / DEFAULT_MODEL_NAME
        tmp = MODELS_DIR / f".{DEFAULT_MODEL_NAME}.part"
        downloading = tmp.exists()
        downloaded = dest.exists()
        size_mb = 0
        progress_mb = 0
        if downloaded:
            size_mb = dest.stat().st_size // (1024 * 1024)
        if downloading:
            progress_mb = tmp.stat().st_size // (1024 * 1024)
        return {
            "downloaded": downloaded,
            "downloading": downloading,
            "path": str(dest),
            "size_mb": size_mb,
            "progress_mb": progress_mb,
            "total_mb": 698,
        }

    async def download_model(self) -> bool:
        MODELS_DIR.mkdir(parents=True, exist_ok=True)
        dest = MODELS_DIR / DEFAULT_MODEL_NAME
        tmp = MODELS_DIR / f".{DEFAULT_MODEL_NAME}.part"
        if dest.exists():
            return True
        asyncio.create_task(self._do_download(dest, tmp))
        return True

    async def _do_download(self, dest: Path, tmp: Path) -> None:
        proc = await asyncio.create_subprocess_exec(
            "curl", "-fSL", "-o", str(tmp), "-C", "-", DEFAULT_MODEL_URL,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.PIPE,
            env=_user_env(),
        )
        _, stderr = await proc.communicate()
        if proc.returncode != 0:
            decky.logger.error(f"Model download failed: {stderr.decode()}")
            return
        tmp.rename(dest)
        await self.set_option("model", str(dest))
        decky.logger.info("Model download complete")

    async def cancel_download(self) -> None:
        tmp = MODELS_DIR / f".{DEFAULT_MODEL_NAME}.part"
        await _run(["pkill", "-f", f"curl.*{DEFAULT_MODEL_NAME}"])
        if tmp.exists():
            tmp.unlink()

    async def start_recording(self) -> bool:
        return await self._send_command("start")

    async def stop_recording(self) -> bool:
        return await self._send_command("stop")

    async def _send_command(self, cmd: str) -> bool:
        import socket
        sock_path = f"{_runtime_dir()}/whisker.sock"
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

        sock_path = f"{_runtime_dir()}/whisker.sock"
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
