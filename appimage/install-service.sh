#!/bin/bash
# SPDX-License-Identifier: MIT
set -eu

UDEV_RULE=/etc/udev/rules.d/70-whisker.rules

APPIMAGE_PATH="${APPIMAGE:-$HOME/Applications/Whisker-x86_64.AppImage}"
APPIMAGE_PATH="$(readlink -f "$APPIMAGE_PATH")"

mkdir -p "$HOME/.config/systemd/user"
cat > "$HOME/.config/systemd/user/whisker.service" <<EOF
[Unit]
Description=Whisker speech-to-text daemon
After=graphical-session.target

[Service]
Type=simple
ExecStart=${APPIMAGE_PATH}
Restart=on-failure
RestartSec=3
# Signal only whisker; killing the AppImage FUSE helper at the same time unmaps the binary mid-shutdown (SIGBUS)
KillMode=mixed
Environment=WHISKER_SERVICE=1
PassEnvironment=WAYLAND_DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS

[Install]
WantedBy=default.target
EOF
chmod 644 "$HOME/.config/systemd/user/whisker.service"

cat > "$HOME/.config/systemd/user/whisker-update.service" <<EOF
[Unit]
Description=Whisker update (signed releases only)
After=network-online.target

[Service]
Type=oneshot
ExecStart=${APPIMAGE_PATH} --auto-update
Environment=WHISKER_SERVICE=1
EOF
cat > "$HOME/.config/systemd/user/whisker-update.timer" <<EOF
[Unit]
Description=Daily Whisker update check

[Timer]
OnCalendar=daily
RandomizedDelaySec=4h
Persistent=true

[Install]
WantedBy=timers.target
EOF
chmod 644 "$HOME/.config/systemd/user/whisker-update.service" "$HOME/.config/systemd/user/whisker-update.timer"

systemctl --user daemon-reload
systemctl --user enable whisker
systemctl --user enable --now whisker-update.timer

systemctl --user import-environment WAYLAND_DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS 2>/dev/null || true

# Root can't read another user's FUSE mount, so the rule goes through a private copy. The path is
# passed as an argument, never spliced into the root shell command.
install_udev_rule() {
    local src="$1" dir
    dir=$(mktemp -d)
    install -m644 -- "$src" "$dir/70-whisker.rules"
    # shellcheck disable=SC2016
    pkexec /bin/sh -c 'install -Dm644 -- "$1" "$2" && udevadm control --reload-rules' sh \
        "$dir/70-whisker.rules" "$UDEV_RULE" \
        || echo "whisker: the foot pedal udev rule was not installed; rerun --install-service to retry" >&2
    rm -rf -- "$dir"
}

RULES_SRC="${APPDIR:-$(dirname "$(readlink -f "$0")")}/70-whisker.rules"
if [ -f "$RULES_SRC" ] && [ ! -f "$UDEV_RULE" ]; then
    install_udev_rule "$RULES_SRC"
fi
