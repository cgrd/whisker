# SPDX-License-Identifier: MIT
import hashlib
import importlib.util
import json
import zipfile
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "make-manifest.py"
spec = importlib.util.spec_from_file_location("make_manifest", SCRIPT)
make_manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(make_manifest)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@pytest.fixture
def dist(tmp_path):
    (tmp_path / "Whisker-x86_64.AppImage").write_bytes(b"appimage bytes")
    with zipfile.ZipFile(tmp_path / "Whisker-x86_64.AppImage.zip", "w", zipfile.ZIP_STORED) as z:
        z.write(tmp_path / "Whisker-x86_64.AppImage", "Whisker-x86_64.AppImage")
    (tmp_path / "install.sh").write_text("#!/bin/sh\n")
    (tmp_path / "whisker-manifest.json.minisig").write_text("old")
    return tmp_path


def test_manifest_lists_every_asset_and_the_appimage(dist):
    manifest = make_manifest.build_manifest("v1.3.0", dist, "1.3.0")
    assert manifest["name"] == "whisker"
    assert manifest["version"] == "1.3.0"
    assert set(manifest["assets"]) == {"Whisker-x86_64.AppImage.zip", "install.sh"}
    assert manifest["assets"]["install.sh"] == {"size": 10, "sha256": sha(b"#!/bin/sh\n")}
    assert manifest["appimage"] == {"size": 14, "sha256": sha(b"appimage bytes")}


def test_main_writes_the_manifest(dist, monkeypatch):
    monkeypatch.setattr(make_manifest, "project_version", lambda _: "1.3.0")
    make_manifest.main(["make-manifest.py", "v1.3.0", str(dist)])
    assert json.loads((dist / "whisker-manifest.json").read_text())["version"] == "1.3.0"


@pytest.mark.parametrize("tag, version", [
    ("1.3.0", "1.3.0"),
    ("v1.3", "1.3"),
    ("v1.3.0", "1.2.0"),
    ("latest", "1.3.0"),
])
def test_rejects_bad_or_mismatched_tags(dist, tag, version):
    with pytest.raises(SystemExit):
        make_manifest.build_manifest(tag, dist, version)


def test_rejects_a_deflated_appimage(dist):
    with zipfile.ZipFile(dist / "Whisker-x86_64.AppImage.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Whisker-x86_64.AppImage", b"appimage bytes")
    with pytest.raises(SystemExit, match="stored uncompressed"):
        make_manifest.build_manifest("v1.3.0", dist, "1.3.0")


def test_reads_the_project_version():
    assert make_manifest.project_version(make_manifest.ROOT / "CMakeLists.txt").count(".") == 2
