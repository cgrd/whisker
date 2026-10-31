# SPDX-License-Identifier: MIT
"""Runs scripts/install.sh against a fake release: an AppImage that is a shell script, with sudo
running commands as the current user and systemd skipped."""
import hashlib
import os
import subprocess
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "install.sh"
APPIMAGE = "Whisker-x86_64.AppImage"

FAKE_APPIMAGE = """#!/bin/sh
case "$1" in
    --version) echo "whisker {version}" ;;
esac
"""


def write_release(dist: Path, version: str = "9.9.9") -> None:
    dist.mkdir(parents=True, exist_ok=True)
    app = dist / APPIMAGE
    app.write_text(FAKE_APPIMAGE.format(version=version))
    with zipfile.ZipFile(dist / f"{APPIMAGE}.zip", "w", zipfile.ZIP_STORED) as z:
        z.write(app, APPIMAGE)
    app.unlink()
    with zipfile.ZipFile(dist / "Whisker-decky.zip", "w") as z:
        z.writestr("Whisker/main.py", "# plugin\n")
    sums = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in sorted(dist.glob("*.zip"))]
    (dist / "SHA256SUMS").write_text("\n".join(sums) + "\n")


@pytest.fixture
def setup(tmp_path):
    dist = tmp_path / "dist"
    write_release(dist)
    home = tmp_path / "home"
    (home / "homebrew" / "plugins").mkdir(parents=True)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "sudo").write_text('#!/bin/sh\nexec "$@"\n')
    (bin_dir / "systemctl").write_text("#!/bin/sh\nexit 0\n")
    for path in bin_dir.iterdir():
        path.chmod(0o755)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    env.update({
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "HOME": str(home),
        "WHISKER_SKIP_SYSTEMD": "1",
        "WHISKER_RELEASE_URL": f"file://{dist}",
    })
    return env, home


def run(env, cwd=None):
    return subprocess.run(["bash", str(SCRIPT)], env=env, cwd=cwd, capture_output=True, text=True)


def test_installs_appimage_and_plugin(setup):
    env, home = setup
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert (home / "Applications" / APPIMAGE).is_file()
    assert (home / "homebrew" / "plugins" / "Whisker" / "main.py").is_file()


def test_planted_zipfile_module_in_cwd_is_not_imported(setup, tmp_path):
    env, home = setup
    evil = tmp_path / "evil"
    evil.mkdir()
    marker = tmp_path / "pwned"
    (evil / "zipfile.py").write_text(f"open({str(marker)!r}, 'w').close()\n")
    result = run(env, cwd=evil)
    assert result.returncode == 0, result.stderr
    assert not marker.exists(), "install.sh imported zipfile.py from the current directory"
    assert (home / "homebrew" / "plugins" / "Whisker" / "main.py").is_file()
