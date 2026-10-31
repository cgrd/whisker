# Whisker

Speech-to-text for Linux and the Steam Deck. Hold a button or foot pedal, speak, release, and your words are typed into whatever window has focus.

Transcription runs locally with an NVIDIA Parakeet model (Vulkan on desktop GPUs; the Decky plugin picks CPU so transcription doesn't take GPU time from the game). Nothing is sent to the cloud.

Needs a 64-bit x86 CPU with AVX2 (x86-64-v3): Intel Haswell (2013), AMD Zen 1 or newer, which includes the Steam Deck.

## Install

### One line (desktop Linux and Steam Deck)

Open a terminal (on a Steam Deck: switch to Desktop Mode and open Konsole) and run:

```sh
curl -fsSL https://github.com/cgrd/whisker/releases/latest/download/install.sh | bash
```

The script downloads the latest release, checks it against the signed release manifest (with your installed Whisker, or `minisign` if you have it; a first install without either trusts the download, see [docs/updates.md](docs/updates.md#installer)), installs the AppImage to `~/Applications`, sets up a background service and, if [Decky Loader](https://decky.xyz) is installed, adds the Whisker plugin to it. Whisker then updates itself once a day from signed releases, never while you are recording ([docs/updates.md](docs/updates.md); turn it off with `auto_update: false`). The Decky panel's Updates section checks when you open it and can install a newer release right away. Running the script again also updates it, Decky plugin included, after your installed Whisker has checked the download's signature. Your settings and models are kept.

It doesn't download a speech model. You pick one (477 to 731 MB, see [Models](#models)): on a desktop the installer opens the settings window on the model list, and on a Steam Deck in Game Mode you pick it in the Whisker panel in Decky's Quick Access menu. Until then the service runs, and pressing a trigger shows "No speech model yet" in the overlay.

It needs `curl` and `python3`, which SteamOS and most distros already have. Installing the Decky plugin asks for your sudo password because Decky's plugin folder belongs to root. On a Deck where you never set one, run `passwd` first.

### Steam Deck without a terminal

1. In Desktop Mode, download [`Install-Whisker.desktop`](https://github.com/cgrd/whisker/releases/latest/download/Install-Whisker.desktop).
2. Double-click it in Dolphin (Downloads folder) and choose Execute.

It runs the same install script in a terminal window.

### Manual install from the zips

Download `Whisker-x86_64.AppImage.zip` and `Whisker-decky.zip` from the [latest release](https://github.com/cgrd/whisker/releases/latest), then from the folder they're in:

```sh
systemctl --user disable --now whisker
rm -f ~/.config/systemd/user/whisker.service ~/Applications/Whisker-x86_64.AppImage ~/.config/autostart/whisker.desktop
mkdir -p ~/Applications
python3 -m zipfile -e Whisker-x86_64.AppImage.zip ~/Applications
chmod +x ~/Applications/Whisker-x86_64.AppImage
~/Applications/Whisker-x86_64.AppImage --install-service
systemctl --user start whisker
# Steam Deck with Decky Loader only:
sudo sh -c 'rm -rf /home/deck/homebrew/plugins/Whisker && python3 -m zipfile -e Whisker-decky.zip /home/deck/homebrew/plugins && systemctl restart plugin_loader'
```

### Verify a download (optional)

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify Whisker-x86_64.AppImage.zip --repo cgrd/whisker
```

Each release also has a minisign-signed `whisker-manifest.json`, which the updater checks; see [docs/updates.md](docs/updates.md#verifying-a-release-by-hand) to verify it yourself.

## Uninstall

```sh
curl -fsSL https://github.com/cgrd/whisker/releases/latest/download/install.sh | bash -s -- --uninstall
```

This keeps your settings (`~/.config/pedal-stt/`) and models and recordings (`~/.local/share/pedal-stt/`). Add `--purge` to delete those too. On a Steam Deck you can also download and run [`Uninstall-Whisker.desktop`](https://github.com/cgrd/whisker/releases/latest/download/Uninstall-Whisker.desktop).

## Use

* **Steam Deck:** hold the L5 back button, speak, release. Leave L5 unbound in each game's Steam Input layout (or pick another button in the Decky plugin).
* **Any controller:** the same button works on every connected controller, see [Controllers](#controllers).
* **Desktop:** hold `ctrl+shift+space`, or the center pedal of an Elgato Stream Deck Pedal.
* A small overlay shows that it's listening and then shows the text.
* Say "period", "comma", "question mark" or "new line" to insert punctuation.

A pedal can also run a program: set `action: exec` with `exec_press` and/or `exec_release` in `~/.config/pedal-stt/config.yaml` (the Decky plugin can't set these). Commands run without a shell: quotes group arguments, but pipes, `&&`, `$VARIABLES` and `~` aren't expanded. Wrap the command in `sh -c '...'` if you need them.

Change settings from the Whisker entry in your app menu or tray icon, or from Decky's Quick Access menu on the Deck. The settings window's Triggers page records the keyboard shortcut and the controller button for you. Logs: `journalctl --user -u whisker`.

If something doesn't work, see [docs/troubleshooting.md](docs/troubleshooting.md).

## Game chat

Whisker talks in World of Warcraft chat on its own while the game runs. When you press the trigger, it opens the chat box with Enter, then types the message with the channel shortcut your first words name:

| Say | Types |
|---|---|
| "party, pull in five" | `/p Pull in five` |
| "guild chat anyone up for a raid" | `/g Anyone up for a raid` |
| "raid warning stack up" | `/rw Stack up` |
| "whisper Thrall need a summon" | `/w Thrall Need a summon` |
| "slash p ready" | `/p Ready` |

Channels: say, yell, emote, party, instance, raid, raid warning, guild, officer, whisper (also "tell"), reply, general (`/1`) and trade (`/2`). "slash" followed by anything types that shortcut, so "slash dance" types `/dance`. Without a channel the text goes to the channel the chat box was last on. Set `submit_key: enter` to send the message too. A channel on its own ("party") only switches the open chat box and never submits, so you can dictate the message next.

`chat_profile` picks the profile: `auto` (the default) uses the first profile whose `match` patterns hit a running process of yours and changes nothing otherwise, `none` turns game chat off, and a profile name such as `wow` always uses that profile. Pick it on the settings window's Audio page or in the Decky plugin too. Whisker only looks at your processes when you press the trigger; it never polls. In auto it checks your profiles in config order, then the built-in ones, and logs the profile it picked (`chat profile wow (auto)`).

A pattern without a slash matches the program's file name, ignoring case, so `Wow.exe` matches World of Warcraft under Proton or Wine. A pattern with `/` or `\` matches part of the full command line, so `_forever_/Wow.exe` tells WoW Forever apart from retail, which are both `Wow.exe`. The built-in `wow` profile matches `Wow.exe`, `WowClassic.exe`, `WowB.exe`, `WowT.exe`, `WowClassicB.exe` and `WowClassicT.exe`.

Character names often come out wrong, so map how you say them to how they're spelled, and add profiles for other games, in `~/.config/pedal-stt/config.yaml`:

```yaml
chat_profile: auto
chat_profiles:
  wow:
    names:
      thrall: Thrall
      jaina proudmoore: Jaina
  ffxiv:
    open_key: enter
    match: ffxiv_dx11.exe
    channels:
      free_company:
        command: /fc
        say: free company, fc
      tell:
        command: /tell
        say: tell, whisper
        target: true
```

`open_key` is pressed before typing. `match` lists the processes that select the profile in auto. `say` lists the spoken lead-ins; the longest one that matches wins. With `target: true` the next word, or a spoken form from `names`, is the recipient. A custom `wow` profile adds to the built-in one (names and match patterns too), and its channels replace built-in channels with the same name. `auto` and `none` can't be profile names. The Decky plugin and the settings window only pick the profile; edit profiles in the config file.

## Controllers

`trigger_button` works on every connected controller, including ones you turn on after Whisker starts. Set it to one button or a chord of buttons joined with `+`:

```yaml
trigger_button: L5      # lower left back button
trigger_button: L3+R3   # both stick clicks, for controllers without back buttons
```

A chord starts recording when all its buttons are down on the same controller and stops when any of them comes up. Button names: `A`, `B`, `X`, `Y`, `L1`, `R1`, `L2`, `R2`, `L3`, `R3`, `L4`, `L5`, `R4`, `R5`, `select`, `start`, `guide`, `assistant`, `capture`, `up`, `down`, `left`, `right`. `L4`/`R4` are the upper back buttons and `L5`/`R5` the lower ones, the same names Steam uses. `assistant` and `capture` are the two middle buttons on the Google Stadia controller, which no game binds.

The easiest way to pick one is "Press the button you want" on the settings window's Triggers page or in the Decky plugin: press and release the button or chord on any controller and Whisker saves it.

| Controller | Read through | Status |
|---|---|---|
| Steam Deck built-in controller | hidraw | tested |
| Steam Controller (2026) over USB or with the wireless puck | hidraw | tested |
| Steam Controller (2026) over Bluetooth or the Steam Machine receiver | hidraw | untested |
| Steam Controller (2015), wired or dongle | hidraw | untested, no rumble |
| Xbox, PlayStation and other gamepads with a Linux driver | evdev | untested; back paddles need Linux 6.17 or newer, except Xbox Elite |
| Google Stadia controller, USB or Bluetooth | evdev | untested; Assistant and Capture buttons as `assistant` and `capture` |

The recording start and stop rumble goes to the controller you pressed. To add a controller, see [docs/controllers.md](docs/controllers.md).

## Models

Whisker uses NVIDIA Parakeet 0.6B (`parakeet-unified-en-0.6b`) in GGUF format, converted by the transcribe.cpp project and downloaded from [Hugging Face](https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf). All quantizations score about the same (about 1.6% word error rate on LibriSpeech). Nothing is downloaded until you pick one in settings, in the Decky panel or with `./get-model.sh <model>`:

| Model | Size | Notes |
|-------|------|-------|
| 8-bit (Q8_0) | 731 MB | Most accurate. Recommended. |
| 6-bit (Q6_K) | 602 MB | Nearly as accurate as 8-bit, smaller. |
| 5-bit (Q5_K_M) | 541 MB | Smaller again, accuracy close to 6-bit. |
| 4-bit (Q4_K_M) | 477 MB | Smallest and fastest, slightly less accurate. |

They're the same model at different precisions. The original is [nvidia/parakeet-unified-en-0.6b](https://huggingface.co/nvidia/parakeet-unified-en-0.6b). The list lives in `models.json`, which the settings window, the Decky plugin and `get-model.sh` all read. Downloads are pinned to one commit of the Hugging Face repo and checked against the size and SHA-256 recorded there; a file that doesn't match is deleted and the download reports an error. A failed download isn't retried in the background: click Download again. A weekly job opens a PR when the repo has a newer commit.

The model isn't part of Whisker. It's made by NVIDIA and licensed under the [NVIDIA Open Model License](https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license/).

## Build from source

Requirements: CMake, a C++17 compiler, and libevdev, pipewire, libopusenc, wayland, cairo, freetype2, SDL2, libsystemd, libX11, vulkan-headers, glslang, spirv-headers and shaderc from your distro.

```sh
make          # build into build/
make test     # C++ unit tests (ctest) and Decky plugin tests (pytest)
./install.sh  # install to ~/.local and start the service
```

Vulkan is on by default; disable it with `cmake -B build -DWHISKER_VULKAN=OFF`.

Builds target the x86-64-v3 baseline so they run on any supported CPU. For a binary tuned to your own CPU (it may not run on other machines), build with `make CMAKE_ARGS=-DWHISKER_NATIVE=ON`.

### Release build (AppImage)

```sh
./build-container.sh
```

This builds inside an Arch Linux container pinned to a February 2025 snapshot (glibc 2.41 toolchain; the binary needs glibc 2.38 or newer) with podman, runs the tests, and writes all release assets to `dist/`. Third-party libraries are linked statically; only system-dependent ones (glibc, Vulkan, PipeWire, libsystemd, X11/Wayland client libraries) are loaded or bundled as shared libraries.

To move the snapshot date (for example after a SteamOS update), edit `ARCH_MIRROR` in the `Containerfile`.

## Contributing

Bug reports, fixes and features are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; commits need a DCO sign-off (`git commit -s`). Everyone is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security problems privately as described in [SECURITY.md](SECURITY.md).

## License

Whisker is MIT licensed, see [LICENSE](LICENSE). Release builds include third-party components under their own licenses, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved.
