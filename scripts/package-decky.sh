#!/bin/bash
# SPDX-License-Identifier: MIT
# Builds <out-dir>/Whisker-decky.zip, the Decky Loader plugin as a Whisker/ folder.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="$(mkdir -p "${1:-dist}" && cd "${1:-dist}" && pwd)"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

PLUGIN="$STAGE/Whisker"
mkdir -p "$PLUGIN/dist"
cp decky/main.py decky/package.json decky/plugin.json decky/README.md "$PLUGIN/"
cp decky/dist/index.js "$PLUGIN/dist/"
cp -r LICENSE THIRD_PARTY_NOTICES.md LICENSES "$PLUGIN/"

rm -f "$OUT/Whisker-decky.zip"
(cd "$STAGE" && python3 -m zipfile -c "$OUT/Whisker-decky.zip" Whisker)
echo "Built: $OUT/Whisker-decky.zip"
