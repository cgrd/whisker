#!/bin/bash
# SPDX-License-Identifier: MIT
# Exercises scripts/install.sh against local release assets in a throwaway container.
# Expects WHISKER_RELEASE_URL to point at a directory built by package-release.sh.
set -euo pipefail

cd "$(dirname "$0")/.."
export WHISKER_SKIP_SYSTEMD=1 WHISKER_SKIP_MODEL=1 APPIMAGE_EXTRACT_AND_RUN=1
: "${WHISKER_RELEASE_URL:?set WHISKER_RELEASE_URL, e.g. file://$PWD/dist}"

APPIMAGE="$HOME/Applications/Whisker-x86_64.AppImage"
PLUGIN="$HOME/homebrew/plugins/Whisker"
CONFIG="$HOME/.config/pedal-stt/config.yaml"

fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$HOME/homebrew/plugins" "$(dirname "$CONFIG")"
echo "backend: cpu" > "$CONFIG"

bash scripts/install.sh
[ -x "$APPIMAGE" ] || fail "AppImage not installed"
"$APPIMAGE" --version | grep -q '^whisker ' || fail "installed AppImage does not run"
[ -f "$PLUGIN/main.py" ] && [ -f "$PLUGIN/dist/index.js" ] || fail "Decky plugin not installed"
[ -f "$PLUGIN/LICENSE" ] || fail "Decky plugin is missing its LICENSE"
[ -f "$HOME/.local/share/applications/whisker.desktop" ] || fail "menu entry missing"

bash scripts/install.sh
[ -x "$APPIMAGE" ] || fail "reinstall broke the AppImage"
grep -qx "backend: cpu" "$CONFIG" || fail "install touched the user config"

bash scripts/install.sh --uninstall
[ ! -e "$APPIMAGE" ] || fail "uninstall left the AppImage"
[ ! -e "$PLUGIN" ] || fail "uninstall left the Decky plugin"
[ -f "$CONFIG" ] || fail "uninstall removed the config without --purge"

bash scripts/install.sh --uninstall --purge
[ ! -e "$CONFIG" ] || fail "--purge kept the config"

echo "install.sh checks passed"
