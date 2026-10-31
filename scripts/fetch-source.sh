#!/bin/bash
# SPDX-License-Identifier: MIT
# Downloads a source tarball, checks its SHA-256 and unpacks it into the current directory.
#
#   fetch-source.sh https://zlib.net/fossils/zlib-1.3.1.tar.gz <sha256>
set -euo pipefail

usage="usage: fetch-source.sh <https-url> <sha256>"
url="${1:?$usage}"
sha256="${2:?$usage}"
file="$(basename "$url")"

curl -fsSL --proto '=https' --proto-redir '=https' --retry 2 -o "$file" "$url"
echo "$sha256  $file" | sha256sum -c --quiet - || { echo "checksum mismatch for $url" >&2; exit 1; }
tar -xf "$file"
rm -f "$file"
