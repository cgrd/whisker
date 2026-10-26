# SPDX-License-Identifier: MIT
import json
import os
import subprocess
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "get-model.sh"


ABC_SHA256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"


@pytest.fixture
def fake_curl(tmp_path):
    """A curl that records its arguments and writes "abc" to its -o target, with a model table matching it."""
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    log = tmp_path / "curl.log"
    curl = bin_dir / "curl"
    curl.write_text(
        "#!/bin/sh\n"
        f'echo "$*" >> {log}\n'
        'while [ $# -gt 0 ]; do [ "$1" = -o ] && printf abc > "$2"; shift; done\n'
    )
    curl.chmod(0o755)
    table = json.loads((SCRIPT.parent / "models.json").read_text())
    for model in table["models"]:
        model["size"], model["sha256"] = 3, ABC_SHA256
    table_path = tmp_path / "models.json"
    table_path.write_text(json.dumps(table))
    env = {
        **os.environ,
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "HOME": str(tmp_path / "home"),
        "WHISKER_MODELS_JSON": str(table_path),
    }
    return env, log


def run(env, *args):
    return subprocess.run(["bash", str(SCRIPT), *args], env=env, capture_output=True, text=True)


def test_without_arguments_downloads_nothing(fake_curl, tmp_path):
    env, log = fake_curl
    result = run(env)
    assert result.returncode == 2
    assert "Q8_0" in result.stderr and "Q4_K_M" in result.stderr
    assert not log.exists()
    assert not (tmp_path / "home").exists()


def test_unknown_model_downloads_nothing(fake_curl):
    env, log = fake_curl
    assert run(env, "Q9").returncode == 2
    assert not log.exists()


MODELS = ".local/share/pedal-stt/models"


def test_downloads_and_verifies_the_named_model(fake_curl, tmp_path):
    env, log = fake_curl
    result = run(env, "q4_k_m")
    assert result.returncode == 0, result.stderr
    args = log.read_text()
    assert "/resolve/843b1b175ace04f5a89d379c79e2518c0d065b25/parakeet-unified-en-0.6b-Q4_K_M.gguf" in args
    assert "--retry 1" in args
    assert "-A Whisker/" in args and "(+https://github.com/cgrd/whisker)" in args
    assert (tmp_path / "home" / MODELS / "parakeet-unified-en-0.6b-Q4_K_M.gguf").read_bytes() == b"abc"


def test_rejects_a_corrupt_download(fake_curl, tmp_path):
    env, _ = fake_curl
    table = json.loads(Path(env["WHISKER_MODELS_JSON"]).read_text())
    table["models"][3]["sha256"] = "0" * 64
    Path(env["WHISKER_MODELS_JSON"]).write_text(json.dumps(table))
    result = run(env, "Q4_K_M")
    assert result.returncode == 1
    assert "SHA-256" in result.stderr
    assert list((tmp_path / "home" / MODELS).iterdir()) == []


def test_a_failed_download_is_not_retried_in_a_loop(fake_curl, tmp_path):
    env, log = fake_curl
    Path(env["PATH"].split(":")[0], "curl").write_text(f'#!/bin/sh\necho "$*" >> {log}\nexit 22\n')
    result = run(env, "Q8_0")
    assert result.returncode == 1
    assert len(log.read_text().splitlines()) == 1
    assert list((tmp_path / "home" / MODELS).iterdir()) == []
