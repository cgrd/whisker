#!/bin/bash
# SPDX-License-Identifier: MIT
# Collects the exact upstream sources of the LGPL code shipped in a release into
# <out-dir>/Whisker-lgpl-sources.tar, so the source travels with the binaries.
#
#   scripts/fetch-lgpl-sources.sh dist   (expects dist/Whisker-x86_64.AppImage)
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="$(cd "${1:-dist}" && pwd)"
APPIMAGE="$OUT/Whisker-x86_64.AppImage"
# Bundled into decky/dist/index.js; keep in sync with the plugin build
DECKY_API_VERSION=1.1.3

CAIRO_VERSION=$(sed -n 's|.*cairographics.org/releases/cairo-\([0-9.]*\)\.tar\.xz.*|\1|p' Containerfile)
[ -n "$CAIRO_VERSION" ] || { echo "cannot find the cairo version in Containerfile" >&2; exit 1; }

RUNTIME_SIZE=$(APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-offset)
RUNTIME_COMMIT=$(APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-version 2>&1 | sed -n 's|.*type2-runtime/commit/\([0-9a-f]*\).*|\1|p')
FUSE_VERSION=$(head -c "$RUNTIME_SIZE" "$APPIMAGE" | strings | grep -xE '3\.[0-9]+\.[0-9]+' | sort -u)
[ -n "$RUNTIME_COMMIT" ] || { echo "cannot read the AppImage runtime commit" >&2; exit 1; }
[ "$(wc -l <<<"$FUSE_VERSION")" -eq 1 ] || { echo "cannot detect the libfuse version: $FUSE_VERSION" >&2; exit 1; }

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
SRC="$STAGE/whisker-lgpl-sources"
mkdir -p "$SRC"

fetch() { curl -fsSL -o "$SRC/$1" "$2"; }
fetch "cairo-$CAIRO_VERSION.tar.xz" "https://cairographics.org/releases/cairo-$CAIRO_VERSION.tar.xz"
fetch "fuse-$FUSE_VERSION.tar.xz" "https://github.com/libfuse/libfuse/releases/download/fuse-$FUSE_VERSION/fuse-$FUSE_VERSION.tar.xz"
fetch "type2-runtime-$RUNTIME_COMMIT.tar.gz" "https://github.com/AppImage/type2-runtime/archive/$RUNTIME_COMMIT.tar.gz"
fetch "decky-api-$DECKY_API_VERSION.tgz" "https://registry.npmjs.org/@decky/api/-/api-$DECKY_API_VERSION.tgz"

cat > "$SRC/README.txt" <<TXT
Corresponding source for the LGPL-2.1 components in this Whisker release.
Whisker's own source and build instructions: https://github.com/cgrd/whisker

* cairo $CAIRO_VERSION: statically linked into usr/bin/whisker in the AppImage
  (LGPL-2.1-only OR MPL-1.1). Rebuild with your own cairo by editing Containerfile
  and running ./build-container.sh.
* libfuse $FUSE_VERSION: statically linked into the AppImage runtime, built by
  AppImage/type2-runtime at commit $RUNTIME_COMMIT (included).
* @decky/api $DECKY_API_VERSION: bundled into dist/index.js of the Decky plugin.
TXT

(cd "$STAGE" && tar -cf "$OUT/Whisker-lgpl-sources.tar" whisker-lgpl-sources)
echo "Built: $OUT/Whisker-lgpl-sources.tar"
