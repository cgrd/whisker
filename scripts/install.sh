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
set -Eeuo pipefail
# Files go to root-owned and other-user readable places (Decky runs as deck), so never inherit a strict umask
umask 022

RELEASE_URL="${WHISKER_RELEASE_URL:-https://github.com/cgrd/whisker/releases/latest/download}"
SKIP_SYSTEMD="${WHISKER_SKIP_SYSTEMD:-0}"

APP_DIR="$HOME/Applications"
APPIMAGE="$APP_DIR/Whisker-x86_64.AppImage"
CONFIG_DIR="$HOME/.config/pedal-stt"
DATA_DIR="$HOME/.local/share/pedal-stt"
MODELS_DIR="$DATA_DIR/models"
MENU_ENTRY="$HOME/.local/share/applications/whisker.desktop"
DECKY_PLUGINS="$HOME/homebrew/plugins"
UDEV_RULES=(/etc/udev/rules.d/70-whisker.rules /etc/udev/rules.d/70-pedal-stt.rules)
MANIFEST=whisker-manifest.json
# The release signing keys from src/signing_keys.h (scripts/tests/test_install_script.py keeps them in sync)
SIGNING_KEYS=(
    RWQ/sZmHKddY47iThBcwcn10e6QWu5WIpnAqDMDe3k/C4NBhud42ple8
)

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; }
die() { warn "$@"; exit 1; }
trap 'die "command failed (line $LINENO): $BASH_COMMAND"' ERR

retry_hint() {
    echo "Run the installer again in a terminal so sudo can ask for your password:" >&2
    echo "  curl -fsSL $RELEASE_URL/install.sh | bash" >&2
}

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
    user_systemctl disable --now whisker-update.timer 2>/dev/null || true
    user_systemctl disable --now whisker 2>/dev/null || true
    user_systemctl disable --now pedal-stt 2>/dev/null || true
    rm -f "$HOME/.config/systemd/user/whisker.service" \
          "$HOME/.config/systemd/user/whisker-update.service" \
          "$HOME/.config/systemd/user/whisker-update.timer" \
          "$HOME/.config/systemd/user/pedal-stt.service" \
          "$HOME/.config/autostart/whisker.desktop" \
          "$HOME/.local/bin/whisker" \
          "$HOME/.local/bin/pedal-stt" \
          "$APPIMAGE" "$APP_DIR/.Whisker-x86_64.AppImage.previous" \
          "$MENU_ENTRY"
    user_systemctl daemon-reload 2>/dev/null || true
}

remove_decky_plugin() {
    [ -d "$DECKY_PLUGINS/Whisker" ] || return 0
    say "Removing the Decky plugin. The plugin folder belongs to root, so sudo will ask for your password."
    # shellcheck disable=SC2016
    as_root sh -c 'cd / && rm -rf -- "$1" && (systemctl restart plugin_loader 2>/dev/null || true)' sh \
        "$DECKY_PLUGINS/Whisker" \
        || { warn "could not remove the Decky plugin ($DECKY_PLUGINS/Whisker)"; retry_hint; exit 1; }
}

remove_udev_rules() {
    [ "$SKIP_SYSTEMD" = 1 ] && return 0
    local found=()
    for rule in "${UDEV_RULES[@]}"; do
        [ -f "$rule" ] && found+=("$rule")
    done
    [ ${#found[@]} -eq 0 ] && return 0
    say "Removing the foot pedal udev rule. This needs sudo."
    as_root rm -f "${found[@]}" \
        || { warn "could not remove the udev rule"; retry_hint; exit 1; }
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
    curl -q -fL --progress-bar --connect-timeout 30 --speed-limit 1024 --speed-time 60 \
        -o "$dest/$name" "$RELEASE_URL/$name" \
        || die "could not download $name from $RELEASE_URL"
}

download_manifest() {
    local dest="$1"
    curl -q -fsSL --connect-timeout 30 --max-time 120 -o "$dest/$MANIFEST" "$RELEASE_URL/$MANIFEST" 2>/dev/null \
        && curl -q -fsSL --connect-timeout 30 --max-time 120 -o "$dest/$MANIFEST.minisig" \
            "$RELEASE_URL/$MANIFEST.minisig" 2>/dev/null \
        && return 0
    rm -f "$dest/$MANIFEST" "$dest/$MANIFEST.minisig"
    return 1
}

verify_sums() {
    local sums="$1" dir="$2"
    shift 2
    (cd "$dir" && for name in "$@"; do
        grep -E "  \*?$name\$" "$sums" | sha256sum -c --quiet - \
            || die "checksum mismatch for $name, refusing to install"
    done)
}

# Checks the release directory against the manifest whose signature minisign verified.
check_manifest_assets() {
    python3 -I - "$@" <<'PY'
import hashlib, json, os, sys
release, comment = sys.argv[1], sys.argv[2]
signed = {"whisker-manifest.json", "whisker-manifest.json.minisig"}
with open(os.path.join(release, "whisker-manifest.json"), "rb") as f:
    manifest = json.load(f)
if manifest.get("name") != "whisker" or comment != f"whisker v{manifest.get('version')}":
    sys.exit(f"the signed comment '{comment}' does not match the manifest")
files = sorted(set(os.listdir(release)) - signed)
if not files:
    sys.exit("there are no release files to verify")
for name in files:
    path = os.path.join(release, name)
    entry = manifest["assets"].get(name)
    if not entry or os.path.islink(path) or not os.path.isfile(path):
        sys.exit(f"{name} is not in the signed manifest")
    sha, size = hashlib.sha256(), 0
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            sha.update(chunk)
            size += len(chunk)
    if size != entry["size"] or sha.hexdigest() != entry["sha256"]:
        sys.exit(f"{name} does not match the signed manifest")
PY
}

verify_with_minisign() {
    local release="$1" key comment
    for key in "${SIGNING_KEYS[@]}"; do
        comment=$(minisign -V -Q -P "$key" -m "$release/$MANIFEST" -x "$release/$MANIFEST.minisig" 2>/dev/null) \
            || continue
        check_manifest_assets "$release" "$comment" || die "refusing to install a download that doesn't match its signed manifest"
        say "Checked the download against the signed release manifest with minisign"
        return 0
    done
    die "the release manifest is not signed by a Whisker release key, refusing to install"
}

# An installed Whisker checks the download with the keys built into it, so updating through the
# installer is as safe as whisker --update. Only an install with nothing to check against trusts
# the download (and SHA256SUMS, which comes from the same place).
verify_release() {
    local tmp="$1" signed="$2"
    shift 2
    local release="$tmp/release" out rc=0
    if [ -x "$APPIMAGE" ]; then
        if [ "$signed" = 1 ]; then
            # WHISKER_SERVICE keeps an old AppImage that doesn't know the flag from opening its first-run setup
            out=$(WHISKER_SERVICE=1 "$APPIMAGE" --verify-release "$release" 2>&1) || rc=$?
            if [ "$rc" -eq 0 ]; then
                say "$out (checked by your installed Whisker)"
                return 0
            fi
            if [ "$rc" -ne 2 ] || ! grep -q "unknown argument" <<<"$out"; then
                warn "$out"
                die "your installed Whisker could not verify this download, so nothing was changed. If it's broken, run install.sh --uninstall first."
            fi
            say "Your installed Whisker is too old to check the download"
        elif [[ "$RELEASE_URL" == file://* ]]; then
            say "warning: $RELEASE_URL has no signed manifest; trusting these local files"
            verify_sums "$tmp/SHA256SUMS" "$release" "$@"
            return 0
        else
            die "the release has no signed manifest ($MANIFEST), so your install was left as it is"
        fi
    fi
    if [ "$signed" = 1 ] && command -v minisign >/dev/null; then
        verify_with_minisign "$release"
        return 0
    fi
    verify_sums "$tmp/SHA256SUMS" "$release" "$@"
    say "warning: nothing on this system can check the release signature, so this install trusts the download"
    say "from $RELEASE_URL, like any curl | bash. Install minisign to have it checked; updates are always checked."
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
    chmod 644 "$MENU_ENTRY"
}

configured_model() {
    local path
    path=$(sed -n 's/^model:[[:space:]]*//p' "$CONFIG_DIR/config.yaml" 2>/dev/null | tr -d '"' | head -n1)
    echo "${path/#\~/$HOME}"
}

has_model() {
    local configured
    configured=$(configured_model)
    compgen -G "$MODELS_DIR/*.gguf" >/dev/null || { [ -n "$configured" ] && [ -f "$configured" ]; }
}

has_graphical_session() {
    [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${DISPLAY:-}" ]
}

# The speech model is the user's choice, so the installer never downloads one. It opens the
# settings window on the model list instead, where nothing downloads until the user picks a model.
open_model_chooser() {
    has_graphical_session || return 1
    setsid -f "$APPIMAGE" --settings >/dev/null 2>&1 </dev/null
}

install_decky_plugin() {
    local zip="$1"
    say "Installing the Decky plugin. Decky's plugin folder belongs to root, so sudo will ask for your password."
    say "(On a Steam Deck, set one first with 'passwd' if you never have.)"
    # Run from / with python3 -I so nothing in the caller's directory is imported as root
    # shellcheck disable=SC2016
    as_root sh -c 'cd / && rm -rf -- "$2/Whisker" \
        && python3 -I -m zipfile -e "$1" "$2" \
        && chmod -R u=rwX,go=rX "$2/Whisker" \
        && (systemctl restart plugin_loader 2>/dev/null || true)' sh "$zip" "$DECKY_PLUGINS" && return 0
    warn "the Decky plugin was NOT installed or updated, the old version may still be in place"
    retry_hint
    return 1
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
    local release="$tmp/release" signed=1
    mkdir "$release"
    download SHA256SUMS "$tmp"
    for name in "${assets[@]}"; do
        download "$name" "$release"
    done
    download_manifest "$release" || signed=0
    verify_release "$tmp" "$signed" "${assets[@]}"

    say "Removing any previous install (your config is kept)"
    stop_old_install

    mkdir -p "$APP_DIR"
    python3 -I -m zipfile -e "$release/Whisker-x86_64.AppImage.zip" "$tmp/app"
    install -m755 "$tmp/app/Whisker-x86_64.AppImage" "$APPIMAGE"
    "$APPIMAGE" --version >/dev/null || die "the AppImage does not run on this system"
    install_menu_entry

    if [ "$SKIP_SYSTEMD" != 1 ]; then
        say "Setting up the background service"
        "$APPIMAGE" --install-service
        mkdir -p "$CONFIG_DIR"
        touch "$CONFIG_DIR/.setup-done"
        systemctl --user start whisker
    fi

    local decky_failed=0
    if has_decky; then
        install_decky_plugin "$release/Whisker-decky.zip" || decky_failed=1
    fi

    local chooser_opened=0
    if ! has_model && open_model_chooser; then
        chooser_opened=1
    fi

    echo
    say "Whisker $("$APPIMAGE" --version | cut -d' ' -f2) is installed"
    if ! has_model; then
        echo "  * No speech model yet, and none was downloaded. Pick one before dictating:"
        if [ "$chooser_opened" = 1 ]; then
            echo "    the Whisker settings window just opened on the model list."
        else
            echo "    open Whisker settings from your app menu (or run $APPIMAGE --settings)."
        fi
        if has_decky; then
            echo "    In Game Mode, pick it in the Whisker panel in Decky's Quick Access menu."
        fi
    fi
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
    if [ "$decky_failed" = 1 ]; then
        warn "the Decky plugin step failed, so the plugin is not up to date (see above)"
        exit 1
    fi
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
