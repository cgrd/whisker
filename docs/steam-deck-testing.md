# Steam Deck Testing

Notes for testing Whisker on the Steam Deck in Game Mode.

## Connection

* Enable SSH on the Deck (Desktop Mode: `passwd`, then `sudo systemctl enable --now sshd`)
* Connect as `deck@<deck-host>`, where `<deck-host>` is the Deck's hostname or IP
* SteamOS with Gamescope compositor

## Screenshots via SSH

Use the XDG portal (grim/import not installed):

```bash
DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/1000 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
  gdbus call --session --dest org.freedesktop.portal.Desktop \
  --object-path /org/freedesktop/portal/desktop \
  --method org.freedesktop.portal.Screenshot.Screenshot '' '{}'
```

Screenshots save to `~/Pictures/Screenshot_*.png`. Wait ~2s before reading. They only capture the base plane, so the Whisker overlay (a Gamescope overlay window) won't show up in them. Check the journal for `overlay: created gamescope X11 window` instead, or look at the screen.

## Deploying

### AppImage

Install the same way a user would. The AppImage lives in `~/Applications` and `--install-service` writes and enables `~/.config/systemd/user/whisker.service` pointing at it:

```bash
scp dist/Whisker-x86_64.AppImage deck@<deck-host>:~/Applications/
ssh deck@<deck-host> "~/Applications/Whisker-x86_64.AppImage --install-service && systemctl --user restart whisker"
```

To update an existing install, stop the service, copy over the AppImage, start it again. No drop-ins are needed. The daemon picks its overlay backend per recording by probing `$XDG_RUNTIME_DIR` (see below) and falls back to `DISPLAY=:0` for the Gamescope X server.

The unit uses `KillMode=mixed` so systemd signals only whisker on stop. With the default, the AppImage FUSE helper was killed at the same time, which unmapped the binary mid-shutdown and crashed with SIGBUS. Check with `coredumpctl list` after `systemctl --user restart whisker`.

### Decky Plugin

Plugin dir is root-owned at `/home/deck/homebrew/plugins/Whisker/`. Build the zip with `scripts/package-decky.sh` (writes `dist/Whisker-decky.zip`), then:

```bash
scp dist/Whisker-decky.zip deck@<deck-host>:/tmp/
ssh -t deck@<deck-host> "sudo sh -c 'rm -rf /home/deck/homebrew/plugins/Whisker && python3 -m zipfile -e /tmp/Whisker-decky.zip /home/deck/homebrew/plugins && systemctl restart plugin_loader.service'"
```

The plugin exposes every config option and edits `~/.config/pedal-stt/config.yaml` in place, keeping nested blocks like `pedals:` intact.

## Game Mode Input

Steam owns the controller in Game Mode, so no evdev events reach userspace from the back buttons. Whisker reads the controller's raw HID reports from `/dev/hidraw*` instead (`src/deckpad.cpp`). It sets `trigger_button: L4`, `L5`, `R4` or `R5` and reads the same reports in Game Mode and Desktop Mode, alongside whatever Steam Input does with the button. The journal shows `deckpad: found Steam Deck controller at /dev/hidrawN` and `deckpad: listening for L5` on startup.

A Unix socket (`/run/user/1000/whisker.sock`) also accepts `start\n` and `stop\n`. The Decky Record button uses it, and it's the easiest way to test without pressing anything:

```bash
python3 -c "import socket; s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/run/user/1000/whisker.sock'); s.sendall(b'start\n'); s.close()"
# wait, then:
python3 -c "import socket; s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/run/user/1000/whisker.sock'); s.sendall(b'stop\n'); s.close()"
```

If the journal reports `below noise threshold (rms=0.000000)`, the mic is muted. Check with `XDG_RUNTIME_DIR=/run/user/1000 wpctl get-volume @DEFAULT_AUDIO_SOURCE@`.

## Overlay Backends

The overlay picks a backend each time a recording starts:

* `gamescope` (`src/overlay_gamescope.cpp`) when a `gamescope-0` socket (or `$GAMESCOPE_WAYLAND_DISPLAY`) under `$XDG_RUNTIME_DIR` accepts connections. It draws an X11 window on `$DISPLAY` (default `:0`).
* `layer-shell` (`src/overlay_layershell.cpp`) otherwise, on the first Wayland display with wlr-layer-shell (KDE, Sway, Hyprland).

Switching between Game Mode and Desktop Mode works without a restart because the choice happens per recording.

Gamescope composites overlays through three X11 atom-based slots, each holding one window:

* `GAMESCOPE_EXTERNAL_OVERLAY` -> `externalOverlayWindow` (held by MangoApp)
* `STEAM_OVERLAY` (full-width) -> `overlayWindow` (used by Whisker)
* `STEAM_OVERLAY` (small) -> `notificationWindow` (Steam notifications)

Wayland layer-shell surfaces get auto-assigned to `externalOverlayWindow` but X11 takes priority, so MangoApp blocks them. That's why Game Mode uses an X11 window with `STEAM_OVERLAY` + `GAMESCOPE_NO_FOCUS` atoms.

Paint order: game -> external overlay -> steam overlay -> notifications -> cursor.

## Keep Awake

Prevent sleep during SSH testing with transient user units, which survive the SSH session ending:

```bash
systemd-run --user -u keepawake systemd-inhibit --what=sleep:idle:handle-lid-switch:handle-suspend-key \
  --who=whisker-testing --why="remote testing" --mode=block sleep infinity
systemd-run --user -u kdeinhibit kde-inhibit --power --screenSaver sleep infinity
systemd-run --user -u xactivity -E DISPLAY=:0 sh -c 'while true; do xdotool mousemove_relative 0 0; sleep 30; done'
```

Stop them with `systemctl --user stop keepawake kdeinhibit xactivity`.

## Available Tools

* `xdpyinfo`, `xdotool`: yes
* `grim`, `xwd`, `import`: no
* `wayland-info`: yes (for Wayland protocol inspection)
* Python3 with pip (`--user`)
* Gamescope 3.16.23.6

## Automated check

`DECK_HOST=<deck-host> make deck-test` copies `dist/` builds to the Deck, installs the AppImage and the Decky plugin, restarts the service twice and reports PASS/FAIL for service state, `KillMode=mixed`, new core dumps, journal errors, the command socket, the deckpad controller and the mic volume.
