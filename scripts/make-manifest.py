#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes <dist>/whisker-manifest.json: the release version and the size and SHA-256 of every asset
and of the AppImage inside the zip. scripts/sign-release.sh signs it; whisker --update trusts nothing else.

  scripts/make-manifest.py v1.3.0 dist
"""
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = "whisker-manifest.json"
APPIMAGE = "Whisker-x86_64.AppImage"
APPIMAGE_ZIP = APPIMAGE + ".zip"
NOT_ASSETS = {MANIFEST, MANIFEST + ".minisig", APPIMAGE}


def digest(stream) -> dict:
    sha = hashlib.sha256()
    size = 0
    while chunk := stream.read(1 << 20):
        sha.update(chunk)
        size += len(chunk)
    return {"size": size, "sha256": sha.hexdigest()}


def project_version(cmakelists: Path) -> str:
    match = re.search(r"project\(whisker VERSION (\d+\.\d+\.\d+)", cmakelists.read_text())
    if not match:
        raise SystemExit(f"no project version in {cmakelists}")
    return match.group(1)


def build_manifest(tag: str, dist: Path, version: str) -> dict:
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise SystemExit(f"bad tag '{tag}'")
    # A binary reporting another version than its release would be offered the same update forever
    if tag[1:] != version:
        raise SystemExit(f"tag {tag} does not match the built version {version}")
    assets = {}
    for path in sorted(dist.iterdir()):
        if path.is_file() and path.name not in NOT_ASSETS:
            with path.open("rb") as f:
                assets[path.name] = digest(f)
    if APPIMAGE_ZIP not in assets:
        raise SystemExit(f"{dist} has no {APPIMAGE_ZIP}")
    with zipfile.ZipFile(dist / APPIMAGE_ZIP) as archive:
        info = archive.getinfo(APPIMAGE)
        if info.compress_type != zipfile.ZIP_STORED:
            raise SystemExit(f"{APPIMAGE} must be stored uncompressed in {APPIMAGE_ZIP} (see package-release.sh)")
        with archive.open(info) as f:
            appimage = digest(f)
    return {"name": "whisker", "version": version, "assets": assets, "appimage": appimage}


def main(argv: list[str]) -> None:
    if len(argv) != 3:
        raise SystemExit(__doc__)
    tag, dist = argv[1], Path(argv[2])
    manifest = build_manifest(tag, dist, project_version(ROOT / "CMakeLists.txt"))
    (dist / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"wrote {dist / MANIFEST} for {tag} ({len(manifest['assets'])} assets)")


if __name__ == "__main__":
    main(sys.argv)
