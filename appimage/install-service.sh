#!/bin/bash
# SPDX-License-Identifier: MIT

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

RULES_SRC="${APPDIR:-$(dirname "$(readlink -f "$0")")}/70-whisker.rules"
if [ -f "$RULES_SRC" ] && [ ! -f /etc/udev/rules.d/70-whisker.rules ]; then
    TMP_RULES="/tmp/whisker-udev-$$.rules"
    cp "$RULES_SRC" "$TMP_RULES"
    chmod 644 "$TMP_RULES"
    pkexec bash -c "install -Dm644 '$TMP_RULES' /etc/udev/rules.d/70-whisker.rules && udevadm control --reload-rules && rm -f '$TMP_RULES'"
fi
