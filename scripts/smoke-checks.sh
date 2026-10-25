#!/bin/bash
# SPDX-License-Identifier: MIT
# Installs an AppImage as the user service on this machine and checks that it runs.
# Used by deck-test.sh (over SSH) and desktop-smoke.sh. Replaces the current install.
set -uo pipefail

SRC="${1:?usage: smoke-checks.sh <AppImage> [--deck]}"
DECK_CHECKS=0
[ "${2:-}" = --deck ] && DECK_CHECKS=1
APPIMAGE="$HOME/Applications/Whisker-x86_64.AppImage"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

failures=0
pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; failures=$((failures + 1)); }
warn() { echo "WARN  $*"; }

# check <pass message> <fail message> <command...>
check() {
    local ok="$1" bad="$2"
    shift 2
    if "$@"; then pass "$ok"; else fail "$bad"; fi
}

# soft <pass message> <warn message> <command...>
soft() {
    local ok="$1" bad="$2"
    shift 2
    if "$@"; then pass "$ok"; else warn "$bad"; fi
}

service_active() { systemctl --user is-active --quiet whisker; }
quiet() { "$@" >/dev/null 2>&1; }

since=$(date +%s)
systemctl --user stop whisker 2>/dev/null || true
mkdir -p "$(dirname "$APPIMAGE")"
install -m755 "$SRC" "$APPIMAGE"
"$APPIMAGE" --install-service >/dev/null 2>&1

check "runs: $("$APPIMAGE" --version 2>&1)" "--version failed" quiet "$APPIMAGE" --version
check "config parses" "--check-config failed" quiet "$APPIMAGE" --check-config
check "unit has KillMode=mixed" "unit is missing KillMode=mixed (SIGBUS on stop)" \
    test "$(systemctl --user show whisker -p KillMode --value)" = mixed

systemctl --user start whisker
sleep 5
check "service active" "service not active" service_active

# Restart twice to exercise shutdown, where the AppImage FUSE SIGBUS used to happen
systemctl --user restart whisker
sleep 5
systemctl --user restart whisker
sleep 5
check "service survives restarts" "service died after restart" service_active

dumps=$(coredumpctl list --no-legend --since "@$since" 2>/dev/null | grep -ci whisker || true)
check "no new core dumps" "$dumps new whisker core dump(s), see coredumpctl list" test "$dumps" -eq 0

log=$(journalctl --user -u whisker --since "@$since" --no-pager -o cat 2>/dev/null)
errors=$(grep -iE 'sigbus|segfault|core dumped|failed to' <<<"$log" | sort -u)
check "no errors in journal" "journal errors: $errors" test -z "$errors"
check "command socket exists" "no $XDG_RUNTIME_DIR/whisker.sock" test -S "$XDG_RUNTIME_DIR/whisker.sock"

volume=$(wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null || echo unknown)
case "$volume" in
    *MUTED*|"Volume: 0.00") warn "microphone is muted ($volume); recordings will be silent" ;;
    unknown) warn "could not read microphone volume" ;;
    *) pass "microphone $volume" ;;
esac

if [ "$DECK_CHECKS" = 1 ]; then
    soft "deckpad found the controller" "deckpad did not report the controller (trigger_button unset?)" \
        grep -q 'deckpad: found Steam Deck controller' <<<"$log"
    soft "Decky loader active" "Decky plugin_loader not active" systemctl is-active --quiet plugin_loader
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "Smoke test PASSED"
else
    echo "Smoke test FAILED ($failures failure(s))"
fi
exit "$failures"
