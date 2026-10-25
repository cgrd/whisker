#!/bin/bash
# SPDX-License-Identifier: MIT
# Fails if the whisker binary uses AVX-512, which the x86-64-v3 release baseline excludes.
# Accepts the AppImage or a plain binary.
#
#   scripts/check-cpu-baseline.sh dist/Whisker-x86_64.AppImage
set -euo pipefail

target=$(realpath "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [[ $target == *.AppImage ]]; then
    (cd "$work" && "$target" --appimage-extract usr/bin/whisker >/dev/null)
    bin=$work/squashfs-root/usr/bin/whisker
else
    bin=$target
fi

objdump -d --no-show-raw-insn "$bin" > "$work/disasm"
count=$(awk '/%zmm[0-9]|%[xy]mm(1[6-9]|2[0-9]|3[01])([^0-9]|$)|%k[0-7]([^0-9]|$)/ { n++ } END { print n + 0 }' "$work/disasm")

if (( count > 0 )); then
    echo "whisker has $count AVX-512 instructions; it must build for x86-64-v3 (WHISKER_NATIVE=OFF)" >&2
    exit 1
fi
echo "whisker is within the x86-64-v3 baseline"
