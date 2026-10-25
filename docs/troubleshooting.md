# Troubleshooting

Start with the logs and a config check:

```sh
journalctl --user -u whisker -b --no-pager | tail -50
~/Applications/Whisker-x86_64.AppImage --check-config
```

## Nothing happens when I hold the Deck back button

* Check the journal for `deckpad: found Steam Deck controller at /dev/hidrawN` and `deckpad: listening for L5`. If they're missing, `trigger_button` is empty or set to a button Whisker can't read from hidraw (only L4, L5, R4 and R5 work).
* The game's Steam Input layout may bind the same button. Whisker still sees the press, but the game reacts too, and some apps grab it outright (Chiaki bound L5 to "end session" by default). Unbind the button in that game's controller settings or pick another back button.
* In Game Mode Steam owns the controller, so evdev triggers (`trigger_key`, gamepad buttons) don't fire. Use a back button or the Decky Record button.

## It records but types nothing

* `below noise threshold (rms=0.000000)` in the journal means silence. The Deck's mic can sit at volume 0 or muted: check with `wpctl get-volume @DEFAULT_AUDIO_SOURCE@` and raise it in Steam's audio settings. Lower `noise_threshold` if you speak quietly.
* `no model loaded` means the model file is missing. Rerun the install script or download a model in settings.
* `failed to create virtual keyboard` means no access to `/dev/uinput`. Most desktops grant it to the logged-in user; otherwise add a udev rule giving your user access.

## The overlay doesn't show

* Game Mode: the overlay is an X11 window using Gamescope's `STEAM_OVERLAY` slot. The journal should say `overlay: selected gamescope backend`. Screenshots taken through the XDG portal or Steam only capture the game plane, so the overlay never appears in them even when it's on screen.
* Desktop: the overlay needs the wlr-layer-shell protocol (KDE Plasma, Sway, Hyprland and other wlroots compositors). GNOME doesn't support it yet, so there's no overlay there; dictation still works.

## Foot pedal not detected

The Elgato Stream Deck Pedal is read through hidraw, which needs the udev rule that `--install-service` (`70-whisker.rules`) or `./install.sh` (`70-pedal-stt.rules`) installs. Check it exists in `/etc/udev/rules.d/`, then unplug and replug the pedal. The journal says `waiting for pedal...` until it's found.

## The service crashes when stopped or restarted

Older units killed the AppImage's FUSE helper together with Whisker, which crashed with SIGBUS. The unit needs `KillMode=mixed`. Rerun `~/Applications/Whisker-x86_64.AppImage --install-service` and check `coredumpctl list` afterwards.

## Settings changed in Decky don't stick

The plugin edits `~/.config/pedal-stt/config.yaml` in place. If the file is hand-edited with unusual YAML (lists, multi-line values), the plugin's small parser may not understand it. Keep to `key: value` lines and the `pedals:` block as in the sample `config.yaml`.
