# SPDX-License-Identifier: MIT
"""Runs scripts/install.sh against a fake release: an AppImage that is a shell script, with sudo
running commands as the current user, systemd skipped and minisign stubbed or absent."""
import functools
import hashlib
import http.server
import json
import os
import re
import subprocess
import threading
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "install.sh"
APPIMAGE = "Whisker-x86_64.AppImage"
MANIFEST = "whisker-manifest.json"
VERSION = "9.9.9"

# Answers --verify-release with the exit code in ~/verify.rc (2 acts like a release without the flag)
FAKE_APPIMAGE = """#!/bin/sh
case "$1" in
    --version) echo "whisker {version}" ;;
    --verify-release)
        ls "$2" > "$HOME/verify.log"
        rc=$(cat "$HOME/verify.rc" 2>/dev/null || echo 0)
        case "$rc" in
            0) echo "Verified Whisker v{version} against its signed manifest" ;;
            2) echo "whisker: unknown argument '--verify-release'" >&2 ;;
            *) echo "whisker: release verification failed: Whisker-decky.zip does not match its SHA-256" >&2 ;;
        esac
        exit "$rc" ;;
esac
"""


def release_keys(text: str) -> list[str]:
    return re.findall(r'^ *"(RW[A-Za-z0-9+/=]+)",$', text, re.MULTILINE)


def digest(path: Path) -> dict:
    data = path.read_bytes()
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def write_sums(dist: Path) -> None:
    sums = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in sorted(dist.glob("*.zip"))]
    (dist / "SHA256SUMS").write_text("\n".join(sums) + "\n")


def write_release(dist: Path, version: str = VERSION) -> None:
    dist.mkdir(parents=True, exist_ok=True)
    app = dist / APPIMAGE
    app.write_text(FAKE_APPIMAGE.format(version=version))
    with zipfile.ZipFile(dist / f"{APPIMAGE}.zip", "w", zipfile.ZIP_STORED) as z:
        z.write(app, APPIMAGE)
    appimage = digest(app)
    app.unlink()
    with zipfile.ZipFile(dist / "Whisker-decky.zip", "w") as z:
        z.writestr("Whisker/main.py", "# plugin\n")
    write_sums(dist)
    assets = {p.name: digest(p) for p in sorted(dist.iterdir())}
    manifest = {"name": "whisker", "version": version, "assets": assets, "appimage": appimage}
    (dist / MANIFEST).write_text(json.dumps(manifest))
    (dist / f"{MANIFEST}.minisig").write_text("fake signature\n")


@pytest.fixture(scope="session")
def system_bin(tmp_path_factory):
    """PATH without minisign, sudo and systemctl, so tests decide which of them exist."""
    out = tmp_path_factory.mktemp("sysbin")
    for directory in os.environ["PATH"].split(os.pathsep):
        if not os.path.isdir(directory):
            continue
        for name in os.listdir(directory):
            target = Path(directory) / name
            link = out / name
            if name not in ("minisign", "sudo", "systemctl") and not link.exists() and os.access(target, os.X_OK):
                link.symlink_to(target)
    return out


@pytest.fixture
def setup(tmp_path, system_bin):
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
        "PATH": f"{bin_dir}:{system_bin}",
        "HOME": str(home),
        "WHISKER_SKIP_SYSTEMD": "1",
        "WHISKER_RELEASE_URL": f"file://{dist}",
    })
    return env, home, dist, bin_dir


def run(env, cwd=None):
    return subprocess.run(["bash", str(SCRIPT)], env=env, cwd=cwd, capture_output=True, text=True)


def installed(home: Path) -> Path:
    return home / "Applications" / APPIMAGE


def plugin(home: Path) -> Path:
    return home / "homebrew" / "plugins" / "Whisker" / "main.py"


def add_minisign(bin_dir: Path, home: Path, rc: int = 0) -> Path:
    """A minisign that accepts the signature (rc 0) for the trusted comment of VERSION, and logs its arguments."""
    log = home / "minisign.log"
    stub = bin_dir / "minisign"
    stub.write_text(f'#!/bin/sh\necho "$@" >> {log}\n[ {rc} = 0 ] && echo "whisker v{VERSION}"\nexit {rc}\n')
    stub.chmod(0o755)
    return log


def existing_install(home: Path, verify_rc: int) -> str:
    app = installed(home)
    app.parent.mkdir(parents=True)
    app.write_text(FAKE_APPIMAGE.format(version="9.9.8"))
    app.chmod(0o755)
    (home / "verify.rc").write_text(f"{verify_rc}\n")
    plugin(home).parent.mkdir(parents=True)
    plugin(home).write_text("# old plugin\n")
    return app.read_text()


def test_installs_appimage_and_plugin(setup):
    env, home, _, _ = setup
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert installed(home).is_file()
    assert plugin(home).is_file()


def test_planted_zipfile_module_in_cwd_is_not_imported(setup, tmp_path):
    env, home, _, _ = setup
    evil = tmp_path / "evil"
    evil.mkdir()
    marker = tmp_path / "pwned"
    (evil / "zipfile.py").write_text(f"open({str(marker)!r}, 'w').close()\n")
    result = run(env, cwd=evil)
    assert result.returncode == 0, result.stderr
    assert not marker.exists(), "install.sh imported zipfile.py from the current directory"
    assert plugin(home).is_file()


def test_signing_keys_match_the_daemon():
    keys = release_keys((ROOT / "src" / "signing_keys.h").read_text())
    block = re.search(r"^SIGNING_KEYS=\(\n(.*?)\n\)$", SCRIPT.read_text(), re.MULTILINE | re.DOTALL)
    assert keys and block
    assert block.group(1).split() == keys


def test_fresh_install_without_minisign_says_it_trusts_the_download(setup):
    env, home, _, _ = setup
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert "trusts the download" in result.stdout
    assert installed(home).is_file()


def test_fresh_install_checks_the_signature_with_minisign(setup):
    env, home, _, bin_dir = setup
    log = add_minisign(bin_dir, home)
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert "with minisign" in result.stdout and "trusts the download" not in result.stdout
    key = release_keys((ROOT / "src" / "signing_keys.h").read_text())[0]
    assert f"-P {key}" in log.read_text()


def test_fresh_install_refuses_a_bad_signature(setup):
    env, home, _, bin_dir = setup
    add_minisign(bin_dir, home, rc=1)
    result = run(env)
    assert result.returncode != 0
    assert "not signed by a Whisker release key" in result.stderr
    assert not installed(home).exists() and not plugin(home).exists()


def test_fresh_install_refuses_an_asset_that_only_matches_sha256sums(setup):
    """SHA256SUMS comes from the same place as the assets, so a swapped asset with matching sums must still fail."""
    env, home, dist, bin_dir = setup
    add_minisign(bin_dir, home)
    with zipfile.ZipFile(dist / "Whisker-decky.zip", "w") as z:
        z.writestr("Whisker/main.py", "# evil\n")
    write_sums(dist)
    result = run(env)
    assert result.returncode != 0
    assert "Whisker-decky.zip does not match the signed manifest" in result.stderr
    assert not installed(home).exists() and not plugin(home).exists()


def test_existing_install_verifies_with_the_installed_whisker(setup):
    env, home, _, _ = setup
    existing_install(home, 0)
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert "checked by your installed Whisker" in result.stdout
    checked = sorted((home / "verify.log").read_text().split())
    assert checked == sorted([f"{APPIMAGE}.zip", "Whisker-decky.zip", MANIFEST, f"{MANIFEST}.minisig"])
    assert "9.9.9" in installed(home).read_text()
    assert plugin(home).read_text() == "# plugin\n"


def test_existing_install_fails_closed_when_its_whisker_rejects_the_download(setup):
    env, home, _, bin_dir = setup
    add_minisign(bin_dir, home)
    old = existing_install(home, 1)
    result = run(env)
    assert result.returncode != 0
    assert "could not verify this download" in result.stderr
    assert installed(home).read_text() == old
    assert plugin(home).read_text() == "# old plugin\n"


def test_existing_install_too_old_to_verify_falls_back(setup):
    env, home, _, _ = setup
    existing_install(home, 2)
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert "too old to check the download" in result.stdout and "trusts the download" in result.stdout


def test_unsigned_local_build_is_allowed_with_a_warning(setup):
    env, home, dist, _ = setup
    existing_install(home, 0)
    (dist / MANIFEST).unlink()
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert "no signed manifest; trusting these local files" in result.stdout


def test_existing_install_refuses_a_remote_release_without_a_manifest(setup):
    env, home, dist, _ = setup
    old = existing_install(home, 0)
    (dist / MANIFEST).unlink()
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(dist))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        env["WHISKER_RELEASE_URL"] = f"http://127.0.0.1:{server.server_address[1]}"
        result = run(env)
    finally:
        server.shutdown()
    assert result.returncode != 0
    assert "no signed manifest" in result.stderr
    assert installed(home).read_text() == old
