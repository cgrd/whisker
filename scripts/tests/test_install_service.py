# SPDX-License-Identifier: MIT
import os
import pwd
import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "appimage" / "install-service.sh"
RULES = "SUBSYSTEM==\"hidraw\", TAG+=\"uaccess\"\n"


@pytest.fixture
def appdir(tmp_path):
    """install-service.sh in a fake AppDir, aimed at a udev rule path in tmp_path, with pkexec and systemctl stubbed."""
    app = tmp_path / "AppDir"
    app.mkdir()
    rule = tmp_path / "etc" / "70-whisker.rules"
    text = SCRIPT.read_text()
    assert re.search(r"^UDEV_RULE=/etc/udev/rules.d/70-whisker.rules$", text, re.MULTILINE)
    (app / "install-service.sh").write_text(text.replace("UDEV_RULE=/etc/udev/rules.d/70-whisker.rules",
                                                         f"UDEV_RULE={rule}"))
    (app / "70-whisker.rules").write_text(RULES)

    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    log = tmp_path / "pkexec.log"
    # Records how the root command was built and what the file it was given looked like, then runs it
    (bin_dir / "pkexec").write_text(
        "#!/bin/bash\n"
        f'printf "%s\\n" "$@" > {log}\n'
        f'stat -c "%a %U" "$(dirname "$5")" >> {log}\n'
        '"$@"\n'
    )
    for name in ("systemctl", "udevadm"):
        (bin_dir / name).write_text("#!/bin/sh\nexit 0\n")
    for path in bin_dir.iterdir():
        path.chmod(0o755)
    tmpdir = tmp_path / "tmp"
    tmpdir.mkdir()
    env = {
        **os.environ,
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "HOME": str(tmp_path / "home"),
        "APPDIR": str(app),
        "APPIMAGE": str(app / "fake.AppImage"),
        "TMPDIR": str(tmpdir),
    }
    (app / "fake.AppImage").write_text("")
    return env, rule, log, tmpdir


def run(env):
    return subprocess.run(["bash", env["APPDIR"] + "/install-service.sh"], env=env, capture_output=True, text=True)


def test_udev_rule_goes_through_a_private_copy(appdir):
    env, rule, log, tmpdir = appdir
    result = run(env)
    assert result.returncode == 0, result.stderr
    assert rule.read_text() == RULES
    args = log.read_text().splitlines()
    assert args[0] == "/bin/sh" and args[1] == "-c"
    # The root command is a fixed string; the file path is only an argument
    assert str(tmpdir) not in args[2]
    copy = Path(args[4])
    assert copy.parent.parent == tmpdir and copy.name == "70-whisker.rules"
    assert args[-1] == f"700 {pwd.getpwuid(os.getuid()).pw_name}"
    assert not list(tmpdir.iterdir()), "the private copy was left behind"


def test_no_predictable_name_in_shared_tmp(appdir):
    env, rule, log, tmpdir = appdir
    # An attacker's file at the old predictable name must never be what root installs
    for pid in range(1, 4):
        (tmpdir / f"whisker-udev-{pid}.rules").write_text("attacker\n")
    run(env)
    assert rule.read_text() == RULES
    assert "whisker-udev-" not in log.read_text()


def test_existing_rule_is_left_alone(appdir):
    env, rule, log, _ = appdir
    rule.parent.mkdir(parents=True)
    rule.write_text("mine\n")
    assert run(env).returncode == 0
    assert rule.read_text() == "mine\n"
    assert not log.exists()


def test_refused_pkexec_warns_and_still_succeeds(appdir, tmp_path):
    env, rule, _, tmpdir = appdir
    (tmp_path / "bin" / "pkexec").write_text("#!/bin/sh\nexit 126\n")
    result = run(env)
    assert result.returncode == 0
    assert "udev rule was not installed" in result.stderr
    assert not rule.exists()
    assert not list(tmpdir.iterdir())

