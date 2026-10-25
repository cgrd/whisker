#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APPDIR="$PROJECT_DIR/Whisker.AppDir"
APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_VERSION=20251108
RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
APPIMAGETOOL="$SCRIPT_DIR/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
RUNTIME="$SCRIPT_DIR/appimagetool-runtime-$RUNTIME_VERSION-x86_64"

cd "$PROJECT_DIR"

if [ ! -f "build/whisker" ] || [ "$(find src/ CMakeLists.txt -newer build/whisker 2>/dev/null)" ]; then
    echo "Building whisker..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$(nproc)"
fi

echo "Creating AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$SCRIPT_DIR/AppRun" "$APPDIR/"
chmod +x "$APPDIR/AppRun"

cp "$SCRIPT_DIR/whisker.desktop" "$APPDIR/"
cp "$SCRIPT_DIR/whisker.desktop" "$APPDIR/usr/share/applications/"

cp assets/whisker-icon-256.png "$APPDIR/whisker.png"
cp assets/whisker-icon-256.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/whisker.png"

cp build/whisker "$APPDIR/usr/bin/"
strip "$APPDIR/usr/bin/whisker"

cp "$SCRIPT_DIR/install-service.sh" "$APPDIR/"
chmod +x "$APPDIR/install-service.sh"

cp 70-pedal-stt.rules "$APPDIR/70-whisker.rules"

DOC_DIR="$APPDIR/usr/share/doc/whisker"
mkdir -p "$DOC_DIR"
cp -r LICENSE THIRD_PARTY_NOTICES.md LICENSES "$DOC_DIR/"
if [ -d third-party-licenses ]; then
    cp -r third-party-licenses "$DOC_DIR/upstream"
fi

# glibc and friends must come from the host; libvulkan is GPU-driver-specific, and the
# host's GL/EGL drivers crash (settings window) when they get an older bundled libwayland
SKIP_LIBS="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread\.so|libvulkan\.so|libpipewire|libspa|libsystemd|libdbus|libwayland"

echo "Bundling libraries..."
LIBS=$(ldd build/whisker | grep -v "not found" | grep "=> /" | awk '{print $3}' | grep -vE "$SKIP_LIBS" | sort -u)
for lib in $LIBS; do
    cp "$lib" "$APPDIR/usr/lib/"
done

# Also grab indirect deps of bundled libs that aren't already covered
INDIRECT=$(find "$APPDIR/usr/lib" -name '*.so*' -exec ldd {} + 2>/dev/null \
    | grep "=> /" | awk '{print $3}' | grep -vE "$SKIP_LIBS" | sort -u)
for lib in $INDIRECT; do
    base=$(basename "$lib")
    [ -f "$APPDIR/usr/lib/$base" ] && continue
    cp "$lib" "$APPDIR/usr/lib/"
done

# Ship the license files of every bundled shared library's package
if command -v pacman >/dev/null; then
    for lib in $LIBS $INDIRECT; do
        pkg=$(pacman -Qqo "$lib")
        [ -d "/usr/share/licenses/$pkg" ] || continue
        mkdir -p "$DOC_DIR/upstream"
        cp -rn "/usr/share/licenses/$pkg" "$DOC_DIR/upstream/"
    done
fi

find "$APPDIR/usr/lib" -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true

LIB_COUNT=$(find "$APPDIR/usr/lib" -name '*.so*' | wc -l)
echo "Bundled $LIB_COUNT libraries"

# fetch <url> <dest> <sha256>: download once, always verify
fetch() {
    [ -f "$2" ] || curl -fsSL -o "$2" "$1"
    if ! echo "$3  $2" | sha256sum -c --quiet -; then
        rm -f "$2"
        echo "checksum mismatch for $1" >&2
        exit 1
    fi
}
fetch "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
    "$APPIMAGETOOL" "$APPIMAGETOOL_SHA256"
fetch "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_VERSION/runtime-x86_64" \
    "$RUNTIME" "$RUNTIME_SHA256"
chmod +x "$APPIMAGETOOL"

echo "Building AppImage..."
ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$RUNTIME" "$APPDIR" "$PROJECT_DIR/Whisker-x86_64.AppImage"

SIZE=$(du -h "$PROJECT_DIR/Whisker-x86_64.AppImage" | cut -f1)
echo ""
echo "Built: Whisker-x86_64.AppImage ($SIZE)"
