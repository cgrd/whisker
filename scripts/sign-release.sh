#!/bin/bash
# SPDX-License-Identifier: MIT
# Writes <dist>/whisker-manifest.json and signs it with the minisign secret key in
# $WHISKER_SIGNING_KEY ($WHISKER_SIGNING_KEY_PASSWORD if the key has a password), then checks the
# signature against the public keys compiled into Whisker. Fails without a key: no unsigned releases.
#
#   scripts/sign-release.sh v1.3.0 dist
set -euo pipefail

cd "$(dirname "$0")/.."
TAG="${1:?usage: sign-release.sh <tag> <dist-dir>}"
DIST="${2:?usage: sign-release.sh <tag> <dist-dir>}"
MINISIGN_VERSION=0.12
MINISIGN_SHA256=9a599b48ba6eb7b1e80f12f36b94ceca7c00b7a5173c95c3efc88d9822957e73

die() { echo "::error::$*" >&2; exit 1; }

[ -n "${WHISKER_SIGNING_KEY:-}" ] || die "WHISKER_SIGNING_KEY is not set; refusing to publish an unsigned release (see docs/updates.md)"

mapfile -t keys < <(sed -n 's/^ *"\(RW[A-Za-z0-9+/=]*\)",$/\1/p' src/signing_keys.h)
[ ${#keys[@]} -gt 0 ] || die "src/signing_keys.h has no public key; add the release key first (see docs/updates.md)"

umask 077
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

curl -fsSL -o "$work/minisign.tar.gz" \
    "https://github.com/jedisct1/minisign/releases/download/$MINISIGN_VERSION/minisign-$MINISIGN_VERSION-linux.tar.gz"
echo "$MINISIGN_SHA256  $work/minisign.tar.gz" | sha256sum -c --quiet - || die "minisign download checksum mismatch"
tar -xzf "$work/minisign.tar.gz" -C "$work" --warning=no-unknown-keyword minisign-linux/x86_64/minisign
minisign="$work/minisign-linux/x86_64/minisign"

python3 -I scripts/make-manifest.py "$TAG" "$DIST"
manifest="$DIST/whisker-manifest.json"

printf '%s\n' "$WHISKER_SIGNING_KEY" > "$work/key"
if [ -n "${WHISKER_SIGNING_KEY_PASSWORD:-}" ]; then
    printf '%s\n' "$WHISKER_SIGNING_KEY_PASSWORD" | "$minisign" -S -s "$work/key" -m "$manifest" -t "whisker $TAG"
else
    "$minisign" -S -s "$work/key" -m "$manifest" -t "whisker $TAG" </dev/null
fi

for key in "${keys[@]}"; do
    if "$minisign" -V -q -P "$key" -m "$manifest"; then
        echo "signed $manifest for $TAG"
        exit 0
    fi
done
rm -f "$manifest" "$manifest.minisig"
die "the signature does not verify with any key in src/signing_keys.h; WHISKER_SIGNING_KEY is not a release key"
