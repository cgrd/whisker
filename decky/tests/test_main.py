# SPDX-License-Identifier: MIT
import asyncio

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
])
def test_set_option_rejects_invalid(config_path, path, value):
    with pytest.raises(ValueError):
        set_option(path, value)
    assert not config_path.exists()


def test_set_option_keeps_exec_commands_from_the_config_file(config_path):
    config_path.parent.mkdir(parents=True)
    config_path.write_text(SAMPLE)
    set_option("pedals.right.mode", "tap")
    right = main.parse_yaml(config_path.read_text())["pedals"]["right"]
    assert right == {"action": "exec", "exec_press": "notify-send hi", "mode": "tap"}


def test_user_env_points_at_the_user_session():
    env = main._user_env()
    runtime = f"/run/user/{main.os.getuid()}"
    assert env["XDG_RUNTIME_DIR"] == runtime
    assert env["DBUS_SESSION_BUS_ADDRESS"] == f"unix:path={runtime}/bus"


@pytest.mark.parametrize("value, stored", [("L5", "L5"), ("L3+R3", "L3+R3"), ("L1 + R1", "L1+R1"), ("", "")])
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
    assert main.DEFAULT_MODEL_NAME == "parakeet-unified-en-0.6b-Q8_0.gguf"


def test_model_status_reports_decimal_megabytes(tmp_path, monkeypatch):
    monkeypatch.setattr(main, "MODELS_DIR", tmp_path)
    status = asyncio.run(main.Plugin().get_model_status())
    assert status["total_mb"] == 731
    assert not status["downloaded"]
