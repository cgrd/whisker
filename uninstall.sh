#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

echo "=== Whisker Uninstaller ==="
echo ""

# Stop and disable service
systemctl --user disable --now whisker-update.timer 2>/dev/null || true
systemctl --user stop whisker 2>/dev/null || true
systemctl --user disable whisker 2>/dev/null || true

echo "Removing files..."
rm -f "$HOME/.local/bin/whisker"
rm -f "$HOME/.local/share/icons/hicolor/256x256/apps/whisker.png"
rm -f "$HOME/.local/share/applications/whisker.desktop"
rm -f "$HOME/.config/systemd/user/whisker.service"
rm -f "$HOME/.config/systemd/user/whisker-update.service" "$HOME/.config/systemd/user/whisker-update.timer"
rm -f "$HOME/.config/autostart/whisker.desktop"

# Update caches
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
systemctl --user daemon-reload 2>/dev/null || true

echo ""
echo "Whisker uninstalled."
echo "Config and recordings kept at:"
echo "  ~/.config/pedal-stt/"
echo "  ~/.local/share/pedal-stt/"
echo "Delete those manually to remove all data."
