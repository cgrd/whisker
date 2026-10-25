#!/bin/bash
# SPDX-License-Identifier: MIT
# Turns <out-dir>/Whisker-x86_64.AppImage into the full set of release assets.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="${1:-dist}"

[ -f "$OUT/Whisker-x86_64.AppImage" ] || { echo "missing $OUT/Whisker-x86_64.AppImage" >&2; exit 1; }

rm -f "$OUT/Whisker-x86_64.AppImage.zip"
(cd "$OUT" && python3 -m zipfile -c Whisker-x86_64.AppImage.zip Whisker-x86_64.AppImage)
scripts/package-decky.sh "$OUT"
cp scripts/install.sh packaging/Install-Whisker.desktop packaging/Uninstall-Whisker.desktop "$OUT/"

(cd "$OUT" && sha256sum Whisker-x86_64.AppImage.zip Whisker-decky.zip install.sh \
    Install-Whisker.desktop Uninstall-Whisker.desktop > SHA256SUMS)
echo "Release assets in $OUT:"
ls -1 "$OUT"
