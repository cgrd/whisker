#!/bin/bash
# SPDX-License-Identifier: MIT
# Copies upstream license files of statically linked dependencies into $1.
set -e

OUT="$1"
DEPS="${2:-/deps}"
FETCHED="${3:-build/_deps}"
TRANSCRIBE="$FETCHED/transcribe_cpp-src"

copy() {
    local name="$1"
    shift
    mkdir -p "$OUT/$name"
    cp "$@" "$OUT/$name/"
}

copy cairo "$DEPS"/cairo-*/COPYING "$DEPS"/cairo-*/COPYING-LGPL-2.1 "$DEPS"/cairo-*/COPYING-MPL-1.1
copy pixman "$DEPS"/pixman-*/COPYING
copy freetype "$DEPS"/freetype-*/LICENSE.TXT "$DEPS"/freetype-*/docs/FTL.TXT
copy libpng "$DEPS"/libpng-*/LICENSE
copy zlib "$DEPS"/zlib-*/LICENSE
copy opus "$DEPS"/opus-*/COPYING
copy libopusenc "$DEPS"/libopusenc-*/COPYING
copy SDL2 "$DEPS"/SDL2-*/LICENSE.txt
copy libevdev "$DEPS"/libevdev-*/COPYING
copy transcribe.cpp "$TRANSCRIBE/LICENSE" "$TRANSCRIBE/THIRD-PARTY-LICENSES.md"
copy ggml "$TRANSCRIBE/ggml/LICENSE"
copy miniz "$TRANSCRIBE/src/third_party/miniz/LICENSE"
copy monocypher "$FETCHED/monocypher-src/LICENCE.md"
copy silero-vad "$TRANSCRIBE/src/third_party/silero_vad/LICENSE"
copy imgui "$FETCHED/imgui-src/LICENSE.txt"
