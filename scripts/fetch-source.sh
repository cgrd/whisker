#!/bin/bash
# SPDX-License-Identifier: MIT
# Downloads a source tarball, checks its SHA-256 and unpacks it into the current directory.
# Extra URLs are mirrors of the same file, tried in order when a download fails.
#
#   fetch-source.sh https://zlib.net/fossils/zlib-1.3.1.tar.gz <sha256> [mirror-url...]
set -euo pipefail

usage="usage: fetch-source.sh <https-url> <sha256> [mirror-url...]"
url="${1:?$usage}"
sha256="${2:?$usage}"
shift 2
file="$(basename "$url")"

for source in "$url" "$@"; do
    if curl -fsSL --proto '=https' --proto-redir '=https' --retry 2 -o "$file" "$source"; then
        echo "$sha256  $file" | sha256sum -c --quiet - || { echo "checksum mismatch for $source" >&2; exit 1; }
        tar -xf "$file"
        rm -f "$file"
        exit 0
    fi
    echo "download failed: $source" >&2
done
exit 1
