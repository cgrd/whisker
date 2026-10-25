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

out=$(bash scripts/install.sh 2>&1) || { echo "$out"; fail "install exited non-zero"; }
echo "$out"
grep -q "Whisker .* is installed" <<<"$out" || fail "install did not end with the installed message"
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

# sudo fails and the user is not root, like an SSH session without a TTY on a Deck
fakebin=$(mktemp -d)
trap 'rm -rf "$fakebin"' EXIT
printf '#!/bin/sh\nexit 1\n' > "$fakebin/sudo"
printf '#!/bin/sh\n[ "$1" = -u ] && echo 1000 || exec /usr/bin/id "$@"\n' > "$fakebin/id"
chmod +x "$fakebin/sudo" "$fakebin/id"

status=0
out=$(PATH="$fakebin:$PATH" bash scripts/install.sh 2>&1) || status=$?
echo "$out"
[ "$status" -ne 0 ] || fail "install exited 0 although the Decky plugin step failed"
grep -q "Decky plugin was NOT installed" <<<"$out" || fail "failed Decky step printed no error"
grep -q "Whisker .* is installed" <<<"$out" || fail "failed Decky step skipped the summary"
[ -x "$APPIMAGE" ] || fail "failed Decky step broke the AppImage install"
[ ! -e "$PLUGIN" ] || fail "plugin installed despite failing sudo"

echo "install.sh checks passed"
