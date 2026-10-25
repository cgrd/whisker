#!/bin/bash
# SPDX-License-Identifier: MIT
# Installs, updates or removes Whisker using the latest GitHub release.
#
#   curl -fsSL https://github.com/cgrd/whisker/releases/latest/download/install.sh | bash
#   bash install.sh --uninstall [--purge]
#
# Environment overrides (used by CI):
#   WHISKER_RELEASE_URL   where to download release assets from; for testing local builds use
#                         file:///path/to/dist (any curl URL works)
#   WHISKER_SKIP_SYSTEMD  set to 1 to skip systemd and udev steps
#   WHISKER_SKIP_MODEL    set to 1 to skip the model download
set -euo pipefail

RELEASE_URL="${WHISKER_RELEASE_URL:-https://github.com/cgrd/whisker/releases/latest/download}"
SKIP_SYSTEMD="${WHISKER_SKIP_SYSTEMD:-0}"
SKIP_MODEL="${WHISKER_SKIP_MODEL:-0}"

APP_DIR="$HOME/Applications"
APPIMAGE="$APP_DIR/Whisker-x86_64.AppImage"
CONFIG_DIR="$HOME/.config/pedal-stt"
DATA_DIR="$HOME/.local/share/pedal-stt"
MODELS_DIR="$DATA_DIR/models"
MODEL_NAME="parakeet-unified-en-0.6b-Q8_0.gguf"
MODEL_URL="https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/$MODEL_NAME"
MENU_ENTRY="$HOME/.local/share/applications/whisker.desktop"
DECKY_PLUGINS="$HOME/homebrew/plugins"
UDEV_RULES=(/etc/udev/rules.d/70-whisker.rules /etc/udev/rules.d/70-pedal-stt.rules)

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

user_systemctl() {
    [ "$SKIP_SYSTEMD" = 1 ] && return 0
    systemctl --user "$@"
}

require() {
    for cmd in "$@"; do
        command -v "$cmd" >/dev/null || die "'$cmd' is required but not installed"
    done
}

has_decky() {
    [ -d "$DECKY_PLUGINS" ]
}

stop_old_install() {
    user_systemctl disable --now whisker 2>/dev/null || true
    user_systemctl disable --now pedal-stt 2>/dev/null || true
    rm -f "$HOME/.config/systemd/user/whisker.service" \
          "$HOME/.config/systemd/user/pedal-stt.service" \
          "$HOME/.config/autostart/whisker.desktop" \
          "$HOME/.local/bin/whisker" \
          "$HOME/.local/bin/pedal-stt" \
          "$APPIMAGE" \
          "$MENU_ENTRY"
    user_systemctl daemon-reload 2>/dev/null || true
}

remove_decky_plugin() {
    [ -d "$DECKY_PLUGINS/Whisker" ] || return 0
    say "Removing the Decky plugin. The plugin folder belongs to root, so sudo will ask for your password."
    as_root sh -c "rm -rf '$DECKY_PLUGINS/Whisker' && (systemctl restart plugin_loader 2>/dev/null || true)"
}

remove_udev_rules() {
    [ "$SKIP_SYSTEMD" = 1 ] && return 0
    local found=()
    for rule in "${UDEV_RULES[@]}"; do
        [ -f "$rule" ] && found+=("$rule")
    done
    [ ${#found[@]} -eq 0 ] && return 0
    say "Removing the foot pedal udev rule. This needs sudo."
    as_root rm -f "${found[@]}"
    as_root udevadm control --reload-rules || true
}

do_uninstall() {
    local purge="$1"
    say "Stopping and removing Whisker"
    stop_old_install
    remove_decky_plugin
    remove_udev_rules
    if [ "$purge" = 1 ]; then
        rm -rf "$CONFIG_DIR" "$DATA_DIR"
        say "Removed config, models and recordings"
    else
        say "Kept your config ($CONFIG_DIR) and models/recordings ($DATA_DIR). Use --purge to delete them."
    fi
    say "Whisker is uninstalled"
}

download() {
    local name="$1" dest="$2"
    curl -fL --progress-bar -o "$dest/$name" "$RELEASE_URL/$name" \
        || die "could not download $name from $RELEASE_URL"
}

verify() {
    local dir="$1"
    shift
    (cd "$dir" && for name in "$@"; do
        grep -E "  \*?$name\$" SHA256SUMS | sha256sum -c --quiet - \
            || die "checksum mismatch for $name, refusing to install"
    done)
}

install_menu_entry() {
    mkdir -p "$(dirname "$MENU_ENTRY")"
    cat > "$MENU_ENTRY" <<EOF
[Desktop Entry]
Type=Application
Name=Whisker
Comment=Speech-to-text settings
Exec="$APPIMAGE" --settings
Icon=audio-input-microphone
Terminal=false
Categories=Utility;Audio;
EOF
}

configured_model() {
    local path
    path=$(sed -n 's/^model:[[:space:]]*//p' "$CONFIG_DIR/config.yaml" 2>/dev/null | tr -d '"' | head -n1)
    echo "${path/#\~/$HOME}"
}

download_model() {
    [ "$SKIP_MODEL" = 1 ] && return 0
    local configured
    configured=$(configured_model)
    if compgen -G "$MODELS_DIR/*.gguf" >/dev/null || { [ -n "$configured" ] && [ -f "$configured" ]; }; then
        say "Speech model already present"
        return 0
    fi
    say "Downloading the speech model (about 700 MB)"
    mkdir -p "$MODELS_DIR"
    curl -fL --progress-bar -o "$MODELS_DIR/.$MODEL_NAME.part" "$MODEL_URL" \
        || die "model download failed; run this script again to retry"
    mv "$MODELS_DIR/.$MODEL_NAME.part" "$MODELS_DIR/$MODEL_NAME"
}

install_decky_plugin() {
    local zip="$1"
    say "Installing the Decky plugin. Decky's plugin folder belongs to root, so sudo will ask for your password."
    say "(On a Steam Deck, set one first with 'passwd' if you never have.)"
    as_root sh -c "rm -rf '$DECKY_PLUGINS/Whisker' \
        && python3 -m zipfile -e '$zip' '$DECKY_PLUGINS' \
        && (systemctl restart plugin_loader 2>/dev/null || true)"
}

do_install() {
    require curl python3 sha256sum
    [ "$(uname -m)" = x86_64 ] || die "Whisker only ships x86_64 builds"
    if [ "$SKIP_SYSTEMD" != 1 ]; then
        require systemctl
        systemctl --user show-environment >/dev/null 2>&1 \
            || die "no systemd user session found; run this from your desktop session"
    fi
    if ! command -v fusermount3 >/dev/null && ! command -v fusermount >/dev/null; then
        say "warning: FUSE tools not found; the AppImage may need APPIMAGE_EXTRACT_AND_RUN=1"
    fi

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "$WORK_DIR"' EXIT
    local tmp="$WORK_DIR"

    local assets=(Whisker-x86_64.AppImage.zip)
    has_decky && assets+=(Whisker-decky.zip)

    say "Downloading Whisker"
    download SHA256SUMS "$tmp"
    for name in "${assets[@]}"; do
        download "$name" "$tmp"
    done
    verify "$tmp" "${assets[@]}"

    say "Removing any previous install (your config is kept)"
    stop_old_install

    mkdir -p "$APP_DIR"
    python3 -m zipfile -e "$tmp/Whisker-x86_64.AppImage.zip" "$tmp/app"
    install -m755 "$tmp/app/Whisker-x86_64.AppImage" "$APPIMAGE"
    "$APPIMAGE" --version >/dev/null || die "the AppImage does not run on this system"
    install_menu_entry

    download_model

    if [ "$SKIP_SYSTEMD" != 1 ]; then
        say "Setting up the background service"
        "$APPIMAGE" --install-service
        mkdir -p "$CONFIG_DIR"
        touch "$CONFIG_DIR/.setup-done"
        systemctl --user start whisker
    fi

    has_decky && install_decky_plugin "$tmp/Whisker-decky.zip"

    echo
    say "Whisker $("$APPIMAGE" --version | cut -d' ' -f2) is installed"
    if has_decky; then
        echo "  * Hold the L5 back button, speak, release. Your words are typed into the focused app."
        echo "  * Leave L5 unbound in each game's Steam Input layout, or pick another button."
        echo "  * Change settings from the Whisker entry in Decky's Quick Access menu."
    else
        echo "  * Press and hold ctrl+shift+space (or the foot pedal), speak, release."
        echo "  * Open settings from your app menu (Whisker) or the tray icon."
    fi
    echo "  * Logs: journalctl --user -u whisker"
    echo "  * Uninstall: curl -fsSL $RELEASE_URL/install.sh | bash -s -- --uninstall"
}

main() {
    local mode=install purge=0
    for arg in "$@"; do
        case "$arg" in
            --uninstall) mode=uninstall ;;
            --purge) purge=1 ;;
            -h|--help) echo "usage: install.sh [--uninstall [--purge]]"; exit 0 ;;
            *) die "unknown option: $arg" ;;
        esac
    done
    [ "$purge" = 1 ] && [ "$mode" != uninstall ] && die "--purge only works with --uninstall"
    if [ "$mode" = uninstall ]; then
        do_uninstall "$purge"
    else
        do_install
    fi
}

main "$@"
