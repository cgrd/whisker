# Troubleshooting

Start with the logs and a config check:

```sh
journalctl --user -u whisker -b --no-pager | tail -50
~/Applications/Whisker-x86_64.AppImage --check-config
```

## Nothing happens when I press the controller button

* Check the journal for `controller: listening for L5` and a `controller: ... streaming on /dev/hidrawN` or `controller: opened ... at /dev/input/eventN` line for your controller. `invalid trigger_button` means the name or chord is wrong; `--check-config` says so too. Use the Decky plugin's "Press the Button You Want" to pick one.
* A wireless Steam Controller shows up as five puck nodes; only the slot your controller is paired to logs `streaming`. If none does, turn the controller on and press a button.
* `cannot open /dev/hidrawN` on a desktop means the Steam udev rules aren't installed. Install your distro's `steam-devices` package (or start Steam once), then replug the controller.
* The game's Steam Input layout may bind the same button. Whisker still sees the press, but the game reacts too, and some apps grab it outright (Chiaki bound L5 to "end session" by default). Unbind the button in that game's controller settings or pick another button or a chord like `L3+R3`.
* Xbox, PlayStation and other controllers are read through evdev, which needs read access to `/dev/input/eventN`. Most desktops grant it to the logged-in user for gamepads.

## It records but types nothing

* `below noise threshold (rms=0.000000)` in the journal means silence. The Deck's mic can sit at volume 0 or muted: check with `wpctl get-volume @DEFAULT_AUDIO_SOURCE@` and raise it in Steam's audio settings. Lower `noise_threshold` if you speak quietly.
* `No speech model yet` (in the journal and the overlay) means no model is picked or the file is gone. Pick one on the Model page in settings or in the Decky panel; Whisker loads it on the next trigger press. `could not load the speech model` means the file is there but broken: download it again.
* `failed to create virtual keyboard` means no access to `/dev/uinput`. Most desktops grant it to the logged-in user; otherwise add a udev rule giving your user access.

## The overlay doesn't show

* Game Mode: the overlay is an X11 window using Gamescope's `STEAM_OVERLAY` slot. The journal should say `overlay: selected gamescope backend`. Screenshots taken through the XDG portal or Steam only capture the game plane, so the overlay never appears in them even when it's on screen.
* Desktop: the overlay needs the wlr-layer-shell protocol (KDE Plasma, Sway, Hyprland and other wlroots compositors). GNOME doesn't support it yet, so there's no overlay there; dictation still works.

## No vibration, or the wrong controller vibrates

Whisker rumbles the controller you pressed the trigger on, if it supports rumble, otherwise the Steam Deck's built-in controller, otherwise nothing. Recordings started from the keyboard, a pedal or Decky rumble the Deck. It never uses virtual pads such as Steam Input's, which forward rumble to whichever controller Steam picks. The journal marks controllers that can rumble with `(rumble)` in the `controller: opened` line; the 2015 Steam Controller has no rumble support yet.

## Foot pedal not detected

The Elgato Stream Deck Pedal is read through hidraw, which needs the udev rule that `--install-service` (`70-whisker.rules`) or `./install.sh` (`70-pedal-stt.rules`) installs. Check it exists in `/etc/udev/rules.d/`, then unplug and replug the pedal. The journal says `waiting for pedal...` until it's found.

## The service crashes when stopped or restarted

Older units killed the AppImage's FUSE helper together with Whisker, which crashed with SIGBUS. The unit needs `KillMode=mixed`. Rerun `~/Applications/Whisker-x86_64.AppImage --install-service` and check `coredumpctl list` afterwards.

## Settings changed in Decky don't stick

The plugin edits `~/.config/pedal-stt/config.yaml` in place. If the file is hand-edited with unusual YAML (lists, multi-line values), the plugin's small parser may not understand it. Keep to `key: value` lines and the `pedals:` block as in the sample `config.yaml`.
