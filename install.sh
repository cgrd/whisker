#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

echo "=== Whisker Installer ==="
echo ""

# Build if needed
if [ ! -f "build/whisker" ] || [ "$(find src/ CMakeLists.txt -newer build/whisker 2>/dev/null)" ]; then
    echo "Building..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -1
    cmake --build build -j"$(nproc)" 2>&1 | tail -1
    echo ""
fi

# Stop existing service
systemctl --user stop whisker 2>/dev/null || true

echo "Installing files..."
install -Dm755 build/whisker "$HOME/.local/bin/whisker"
install -Dm644 assets/whisker-icon-256.png "$HOME/.local/share/icons/hicolor/256x256/apps/whisker.png"
install -Dm644 whisker.desktop "$HOME/.local/share/applications/whisker.desktop"
install -Dm644 whisker.service "$HOME/.config/systemd/user/whisker.service"

# Update caches
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# udev rules for pedal access
if [ -f 70-pedal-stt.rules ]; then
    echo ""
    echo "Setting up pedal permissions (needs password)..."
    if command -v pkexec &>/dev/null; then
        pkexec bash -c "install -Dm644 '$(pwd)/70-pedal-stt.rules' /etc/udev/rules.d/70-pedal-stt.rules && udevadm control --reload-rules"
    else
        sudo install -Dm644 70-pedal-stt.rules /etc/udev/rules.d/70-pedal-stt.rules
        sudo udevadm control --reload-rules
    fi
fi

# Enable and start
systemctl --user daemon-reload
systemctl --user enable --now whisker

echo ""
echo "Whisker installed and running."
echo "Open settings from your app menu, or: whisker --settings"
