#!/bin/bash
# SPDX-License-Identifier: MIT
# Installs a build on a Steam Deck over SSH and checks that it runs.
#
#   DECK_HOST=<deck-host> scripts/deck-test.sh [Whisker-x86_64.AppImage] [Whisker-decky.zip]
#
# Builds missing artifacts with build-container.sh. Then runs scripts/hil-test.py on the Deck
# with virtual controllers (and replugs a Steam Controller puck if one is connected);
# SKIP_HIL=1 skips that.
# Never point this at artifacts from an untrusted PR without reading the diff first.
set -euo pipefail

cd "$(dirname "$0")/.."

: "${DECK_HOST:?set DECK_HOST to the Deck hostname or IP}"
DECK="${DECK_USER:-deck}@$DECK_HOST"
APPIMAGE="${1:-dist/Whisker-x86_64.AppImage}"
DECKY_ZIP="${2:-dist/Whisker-decky.zip}"

[ -f "$APPIMAGE" ] || ./build-container.sh
[ -f "$DECKY_ZIP" ] || scripts/package-decky.sh

echo "==> Copying build to $DECK"
scp -q "$APPIMAGE" "$DECK:/tmp/Whisker-x86_64.AppImage"
scp -q "$DECKY_ZIP" "$DECK:/tmp/Whisker-decky.zip"

echo "==> Installing the Decky plugin (sudo on the Deck)"
ssh -t "$DECK" "sudo sh -c 'rm -rf /home/deck/homebrew/plugins/Whisker \
    && python3 -m zipfile -e /tmp/Whisker-decky.zip /home/deck/homebrew/plugins \
    && systemctl restart plugin_loader'"

echo "==> Installing the AppImage and running checks"
ssh "$DECK" bash -s -- /tmp/Whisker-x86_64.AppImage --deck < scripts/smoke-checks.sh

if [ "${SKIP_HIL:-0}" = 1 ]; then
    exit 0
fi
echo "==> Hardware-in-the-loop test with virtual controllers (sudo on the Deck)"
ssh "$DECK" "rm -rf /tmp/whisker-hil && mkdir -p /tmp/whisker-hil/scripts /tmp/whisker-hil/decky"
scp -q scripts/hil-test.py "$DECK:/tmp/whisker-hil/scripts/"
scp -q decky/main.py "$DECK:/tmp/whisker-hil/decky/"
ssh -t "$DECK" "sudo python3 -I /tmp/whisker-hil/scripts/hil-test.py \
    --whisker /home/deck/Applications/Whisker-x86_64.AppImage --user deck --service --puck"
