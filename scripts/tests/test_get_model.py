# SPDX-License-Identifier: MIT
import os
import subprocess
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "get-model.sh"


@pytest.fixture
def fake_curl(tmp_path):
    """A curl that records its arguments and writes a small file to its -o target."""
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    log = tmp_path / "curl.log"
    curl = bin_dir / "curl"
    curl.write_text(
        "#!/bin/sh\n"
        f'echo "$*" >> {log}\n'
        'while [ $# -gt 0 ]; do [ "$1" = -o ] && echo gguf > "$2"; shift; done\n'
    )
    curl.chmod(0o755)
    env = {**os.environ, "PATH": f"{bin_dir}:{os.environ['PATH']}", "HOME": str(tmp_path / "home")}
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


def test_downloads_the_named_model(fake_curl, tmp_path):
    env, log = fake_curl
    result = run(env, "q4_k_m")
    assert result.returncode == 0, result.stderr
    assert "parakeet-unified-en-0.6b-Q4_K_M.gguf" in log.read_text()
    assert (tmp_path / "home/.local/share/pedal-stt/models/parakeet-unified-en-0.6b-Q4_K_M.gguf").is_file()
