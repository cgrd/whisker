# SPDX-License-Identifier: MIT
import asyncio
import http.server
import logging
import os
import threading
from pathlib import Path

import main
import pytest

SAMPLE = """\
model: ~/.local/share/pedal-stt/models/parakeet.gguf
backend: vulkan

# comment
history: 50
noise_threshold: 0.005
overlay: true
audio_device: "alsa input"
submit_key: none

pedals:
  left:
    action: key
    key: "ctrl+shift+alt+F1"
    mode: hold
  center:
    action: transcribe
    # Output mode
    output: type
  right:
    action: exec
    exec_press: "notify-send hi"
"""


@pytest.fixture
def config_path(tmp_path, monkeypatch):
    path = tmp_path / "pedal-stt" / "config.yaml"
    monkeypatch.setattr(main, "CONFIG_PATH", path)
    return path


def set_option(path, value):
    asyncio.run(main.Plugin().set_option(path, value))


def test_parse_scalars():
    data = main.parse_yaml(SAMPLE)
    assert data["backend"] == "vulkan"
    assert data["history"] == 50
    assert data["noise_threshold"] == 0.005
    assert data["overlay"] is True
    assert data["audio_device"] == "alsa input"
    assert data["submit_key"] == "none"


def test_parse_pedals_block():
    pedals = main.parse_yaml(SAMPLE)["pedals"]
    assert pedals["left"] == {"action": "key", "key": "ctrl+shift+alt+F1", "mode": "hold"}
    assert pedals["center"] == {"action": "transcribe", "output": "type"}
    assert pedals["right"] == {"action": "exec", "exec_press": "notify-send hi"}


def test_key_after_nested_block_returns_to_top_level():
    data = main.parse_yaml("pedals:\n  left:\n    action: key\nbackend: cpu\n")
    assert data["backend"] == "cpu"
    assert data["pedals"] == {"left": {"action": "key"}}


def test_round_trip_keeps_everything():
    data = main.parse_yaml(SAMPLE)
    assert main.parse_yaml(main.serialize_yaml(data)) == data


def test_serialize_quotes_when_needed():
    out = main.serialize_yaml({"a": "", "b": "x y", "c": "k:v", "d": "plain", "e": False, "f": 1.5})
    assert out == 'a: ""\nb: "x y"\nc: "k:v"\nd: plain\ne: false\nf: 1.5\n'


def test_set_option_preserves_pedals(config_path):
    config_path.parent.mkdir(parents=True)
    config_path.write_text(SAMPLE)
    set_option("noise_threshold", 0.01)
    data = main.parse_yaml(config_path.read_text())
    assert data["noise_threshold"] == 0.01
    assert data["pedals"] == main.parse_yaml(SAMPLE)["pedals"]


def test_set_option_nested_creates_missing_blocks(config_path):
    set_option("pedals.right.suffix", "enter")
    assert main.parse_yaml(config_path.read_text()) == {"pedals": {"right": {"suffix": "enter"}}}


def test_set_option_coerces_numbers(config_path):
    set_option("history", 7.6)
    set_option("noise_threshold", 1)
    data = main.parse_yaml(config_path.read_text())
    assert data["history"] == 8
    assert data["noise_threshold"] == 1.0


@pytest.mark.parametrize("path, value", [
    ("bogus", 1),
    ("pedals.middle.action", "key"),
    ("pedals.left.bogus", "x"),
    ("pedals.left", "key"),
    ("backend", "cuda"),
    ("submit_key", "space"),
    ("pedals.left.mode", "press"),
    ("history", "10"),
    ("history", True),
    ("overlay", 1),
    ("trigger_key", 5),
    ("pedals.left.exec_press", "rm -rf ~"),
    ("pedals.left.exec_release", "x"),
    ("pedals.left.action", "exec"),
    ("trigger_button", "L6"),
    ("trigger_button", "L3+L3"),
    ("trigger_button", "L3+"),
    ("trigger_button", "l5"),
    ("trigger_button", 5),
    ("chat_profile", "wwo"),
    ("chat_profile", 1),
    ("chat_profiles.wow.open_key", "enter"),
    ("chat_profiles.wow.channels.party.command", "/p"),
    ("auto_update", "false"),
    ("auto_update", 0),
])
def test_set_option_rejects_invalid(config_path, path, value):
    with pytest.raises(ValueError):
        set_option(path, value)
    assert not config_path.exists()


def test_set_option_turns_off_automatic_updates(config_path):
    set_option("auto_update", False)
    assert main.parse_yaml(config_path.read_text()) == {"auto_update": False}


def test_set_option_keeps_exec_commands_from_the_config_file(config_path):
    config_path.parent.mkdir(parents=True)
    config_path.write_text(SAMPLE)
    set_option("pedals.right.mode", "tap")
    right = main.parse_yaml(config_path.read_text())["pedals"]["right"]
    assert right == {"action": "exec", "exec_press": "notify-send hi", "mode": "tap"}


CHAT_SAMPLE = SAMPLE + """\
chat_profile: wow
chat_profiles:
  wow:
    names:
      thrall: Thrall
      jaina proudmoore: "Jaina"
  ffxiv:
    open_key: enter
    match: "ffxiv_dx11.exe, SquareEnix/FINAL FANTASY XIV"
    channels:
      free_company:
        command: /fc
        say: "free company, fc"
      tell:
        command: "/tell"
        say: [tell, whisper]
        target: true
"""


def test_parse_chat_profiles_block():
    data = main.parse_yaml(CHAT_SAMPLE)
    assert data["chat_profile"] == "wow"
    assert data["chat_profiles"]["wow"] == {"names": {"thrall": "Thrall", "jaina proudmoore": "Jaina"}}
    ffxiv = data["chat_profiles"]["ffxiv"]
    assert ffxiv["open_key"] == "enter"
    assert ffxiv["match"] == "ffxiv_dx11.exe, SquareEnix/FINAL FANTASY XIV"
    assert ffxiv["channels"]["free_company"] == {"command": "/fc", "say": "free company, fc"}
    assert ffxiv["channels"]["tell"] == {"command": "/tell", "say": "[tell, whisper]", "target": True}
    assert data["pedals"] == main.parse_yaml(SAMPLE)["pedals"]


def test_chat_profiles_round_trip():
    data = main.parse_yaml(CHAT_SAMPLE)
    assert main.parse_yaml(main.serialize_yaml(data)) == data


def test_set_chat_profile_keeps_profiles_and_pedals(config_path):
    config_path.parent.mkdir(parents=True)
    config_path.write_text(CHAT_SAMPLE)
    before = main.parse_yaml(CHAT_SAMPLE)
    set_option("chat_profile", "ffxiv")
    set_option("submit_key", "enter")
    data = main.parse_yaml(config_path.read_text())
    assert data["chat_profile"] == "ffxiv"
    assert data["chat_profiles"] == before["chat_profiles"]
    assert data["pedals"] == before["pedals"]
    set_option("chat_profile", "")
    assert main.parse_yaml(config_path.read_text())["chat_profile"] == ""
    for mode in ("auto", "none"):
        set_option("chat_profile", mode)
        data = main.parse_yaml(config_path.read_text())
        assert data["chat_profile"] == mode
        assert data["chat_profiles"] == before["chat_profiles"]


def test_chat_profile_must_exist(config_path):
    set_option("chat_profile", "wow")
    with pytest.raises(ValueError):
        set_option("chat_profile", "ffxiv")
    assert main.parse_yaml(config_path.read_text())["chat_profile"] == "wow"


def test_chat_profile_names(config_path):
    assert main.chat_profile_names({}) == ["wow"]
    assert main.chat_profile_names(main.parse_yaml(CHAT_SAMPLE)) == ["wow", "ffxiv"]
    assert asyncio.run(main.Plugin().get_chat_profiles()) == ["wow"]
    assert main.chat_profile_names({"chat_profiles": {"auto": {}, "none": {}, "eso": {}}}) == ["wow", "eso"]


def test_user_env_points_at_the_user_session():
    env = main._user_env()
    runtime = f"/run/user/{main.os.getuid()}"
    assert env["XDG_RUNTIME_DIR"] == runtime
    assert env["DBUS_SESSION_BUS_ADDRESS"] == f"unix:path={runtime}/bus"


@pytest.mark.parametrize("value, stored", [
    ("L5", "L5"), ("L3+R3", "L3+R3"), ("L1 + R1", "L1+R1"), ("capture", "capture"),
    ("assistant+capture", "assistant+capture"), ("", ""),
])
def test_set_option_accepts_buttons_and_chords(config_path, value, stored):
    set_option("trigger_button", value)
    assert main.parse_yaml(config_path.read_text())["trigger_button"] == stored


@pytest.mark.parametrize("line, reply", [
    ("L3+R3 Steam Controller (puck)\n", {"button": "L3+R3", "device": "Steam Controller (puck)"}),
    ("L5 Steam Deck\n", {"button": "L5", "device": "Steam Deck"}),
    ("\n", {"error": "No button pressed"}),
    ("", {"error": "No button pressed"}),
    ("busy\n", {"error": "Whisker is recording, try again"}),
])
def test_parse_capture_reply(line, reply):
    assert main.parse_capture_reply(line) == reply


def test_parse_capture_reply_rejects_unknown_buttons():
    with pytest.raises(ValueError):
        main.parse_capture_reply("L9 pad\n")


def test_model_labels_come_from_the_shared_table():
    known = asyncio.run(main.Plugin().get_known_models())
    labels = [m["label"] for m in known["models"]]
    assert labels == [
        "Parakeet 0.6B, 8-bit (Q8_0), 731 MB",
        "Parakeet 0.6B, 6-bit (Q6_K), 602 MB",
        "Parakeet 0.6B, 5-bit (Q5_K_M), 541 MB",
        "Parakeet 0.6B, 4-bit (Q4_K_M), 477 MB",
    ]
    assert known["repo"] == "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf"
    assert known["original"] == "https://huggingface.co/nvidia/parakeet-unified-en-0.6b"
    assert [m["recommended"] for m in known["models"]] == [True, False, False, False]


@pytest.fixture
def no_subprocess(monkeypatch):
    async def forbidden(*args, **kwargs):
        raise AssertionError(f"unexpected subprocess: {args}")

    monkeypatch.setattr(asyncio, "create_subprocess_exec", forbidden)


def test_fresh_start_downloads_nothing(tmp_path, monkeypatch, config_path, no_subprocess):
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path / "models")
    plugin = main.Plugin()
    status = asyncio.run(plugin.get_model_status())
    asyncio.run(plugin.get_known_models())
    asyncio.run(plugin.get_models())
    assert status["configured"] == ""
    assert not status["configured_exists"]
    assert status["downloaded"] == []
    assert status["downloading"] == ""
    assert not (tmp_path / "models").exists()


def test_download_requires_a_known_model(tmp_path, monkeypatch, config_path, no_subprocess):
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path)
    with pytest.raises(ValueError):
        asyncio.run(main.Plugin().download_model("../evil.gguf"))


def test_picking_a_downloaded_model_selects_it(tmp_path, monkeypatch, config_path, no_subprocess):
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path)
    model = tmp_path / "parakeet-unified-en-0.6b-Q6_K.gguf"
    model.write_bytes(b"gguf")
    asyncio.run(main.Plugin().download_model(model.name))
    status = asyncio.run(main.Plugin().get_model_status())
    assert status["configured"] == str(model)
    assert status["configured_exists"]
    assert status["downloaded"] == [model.name]


def test_download_progress_reports_decimal_megabytes(tmp_path, monkeypatch, config_path):
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path)
    (tmp_path / ".parakeet-unified-en-0.6b-Q8_0.gguf.part").write_bytes(b"x" * 2_000_000)
    status = asyncio.run(main.Plugin().get_model_status())
    assert status["downloading"] == "parakeet-unified-en-0.6b-Q8_0.gguf"
    assert status["progress_mb"] == 2
    assert status["total_mb"] == 731


def test_model_urls_are_pinned_to_a_commit():
    revision = main.MODEL_TABLE["revision"]
    assert len(revision) == 40
    for model in main.MODEL_TABLE["models"]:
        assert main.model_url(model).endswith(f"/resolve/{revision}/{model['file']}")
        assert len(model["sha256"]) == 64


def test_downloads_identify_whisker():
    args = main.curl_download_args("https://example.invalid/m.gguf", Path("/tmp/m.part"))
    agent = args[args.index("-A") + 1]
    assert agent.startswith("Whisker/") and agent.endswith("(+https://github.com/cgrd/whisker)")
    assert args[args.index("--retry") + 1] == "1"


ABC_SHA256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"


def test_install_verified_accepts_a_matching_file(tmp_path):
    tmp, dest = tmp_path / ".m.part", tmp_path / "m.gguf"
    tmp.write_bytes(b"abc")
    assert main.install_verified(tmp, dest, 3, ABC_SHA256) == ""
    assert dest.read_bytes() == b"abc"
    assert not tmp.exists()


@pytest.mark.parametrize(("size", "sha256", "message"), [
    (3, "0" * 64, "SHA-256"),
    (4, ABC_SHA256, "wrong size"),
])
def test_install_verified_deletes_a_bad_file(tmp_path, size, sha256, message):
    tmp, dest = tmp_path / ".m.part", tmp_path / "m.gguf"
    tmp.write_bytes(b"abc")
    assert message in main.install_verified(tmp, dest, size, sha256)
    assert not tmp.exists()
    assert not dest.exists()


@pytest.fixture
def failing_server():
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            self.send_response(503)
            self.send_header("Content-Length", "0")
            self.end_headers()

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{server.server_address[1]}", requests
    server.shutdown()


def test_failed_download_is_retried_at_most_once(tmp_path, monkeypatch, config_path, failing_server):
    url, requests = failing_server
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path)
    monkeypatch.setattr(main, "model_url", lambda model: f"{url}/{model['file']}")
    monkeypatch.setattr(main, "_user_env", lambda: dict(os.environ))
    monkeypatch.setattr(main.decky, "logger", logging.getLogger("test"), raising=False)
    plugin = main.Plugin()
    model = main.known_model("parakeet-unified-en-0.6b-Q4_K_M.gguf")
    dest = tmp_path / model["file"]
    asyncio.run(plugin._do_download(model, dest, main._part_path(model["file"])))
    assert 1 <= len(requests) <= 2
    assert not dest.exists()
    assert not main._part_path(model["file"]).exists()
    assert "Download failed" in asyncio.run(plugin.get_model_status())["error"]


def test_default_backend_to_cpu_only_when_unset():
    config = {"model": "m.gguf"}
    assert main.default_backend_to_cpu(config)
    assert config["backend"] == "cpu"
    config = {"backend": "vulkan"}
    assert not main.default_backend_to_cpu(config)
    assert config["backend"] == "vulkan"
