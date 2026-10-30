#!/bin/bash
# SPDX-License-Identifier: MIT
# Creates a release signing key. The secret key goes straight into the WHISKER_SIGNING_KEY secret of
# the release environment and your password manager; only the public key is printed, for
# src/signing_keys.h. With --next (key rotation, see docs/updates.md) the secret isn't set.
#
#   scripts/gen-signing-key.sh [--next]
set -euo pipefail

REPO=cgrd/whisker
SECRET=WHISKER_SIGNING_KEY

die() { echo "error: $*" >&2; exit 1; }

next=0
case "${1:-}" in
    --next) next=1 ;;
    "") ;;
    *) die "usage: gen-signing-key.sh [--next]" ;;
esac
for cmd in minisign gh shred; do
    command -v "$cmd" >/dev/null || die "'$cmd' is required"
done
if [ "$next" != 1 ] && gh secret list -R "$REPO" --env release | grep -q "^${SECRET}[[:space:]]"; then
    die "$REPO already has $SECRET. To rotate, follow docs/updates.md and pass --next"
fi

umask 077
# XDG_RUNTIME_DIR is a tmpfs, so the key never reaches a disk
dir=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/whisker-key.XXXXXX")
cleanup() {
    if [ -f "$dir/whisker.key" ]; then shred -u "$dir/whisker.key"; fi
    rm -rf -- "$dir"
}
trap cleanup EXIT

minisign -G -W -p "$dir/whisker.pub" -s "$dir/whisker.key" >/dev/null
if [ "$next" = 1 ]; then
    echo "Not stored on GitHub yet (--next)."
else
    gh secret set "$SECRET" -R "$REPO" --env release < "$dir/whisker.key"
    echo "Stored the secret key as $SECRET in the release environment of $REPO."
fi
echo
echo "Public key, add it to src/signing_keys.h:"
echo "    \"$(tail -n 1 "$dir/whisker.pub")\","
echo
echo "Back up the secret key now: copy the contents of $dir/whisker.key into your password manager."
echo "GitHub never shows a secret again, and without a backup a lost secret means rotating the key."
read -r -p "Press Enter once it's backed up; the file is then shredded. " _
