# Notes for AI coding agents (and humans)

Read [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow, code style and review rules. This file adds the context that's easy to get wrong.

## Layout

* `src/`: the `whisker` daemon (C++17), one module per concern
  * `main.cpp` event loop and CLI; `config.*` YAML config; `dictation.*` text post-processing
  * triggers: `pedal.*` (Elgato pedal, hidraw), `hotkey.*` (keyboard, evdev), `controllers.*` (every gamepad: Valve devices over hidraw, others over evdev, hotplug, button capture), `buttons.*` (pure report decoders and chords), `command.*` (Unix socket)
  * `haptic.*` rumbles the controller that triggered the recording
  * output: `keyboard.*` (uinput virtual keyboard), `clipboard.*`
  * audio: `audio.*` (PipeWire capture), `audiocue.*`, `stt.*` (transcribe.cpp), `history.*`
  * UI: `overlay*.cpp` (backend chosen per recording), `settings.*` (SDL2 + Dear ImGui), `tray.*` (StatusNotifierItem over sd-bus)
* `tests/`: C++ unit tests (plain asserts, `tests/check.h`) and CLI smoke tests, run by ctest
* `decky/`: Decky Loader plugin. `main.py` is the Python backend, `src/index.tsx` the UI, `dist/index.js` its committed build (`make decky`), `tests/` pytest
* `appimage/`: AppRun, service installer, AppImage packaging; `Containerfile` + `build-container.sh` produce release builds
* `scripts/`: user installer (`install.sh`), release packaging, CI helpers (`check_conventions.py`, `review-flags.sh`, `check-dco.sh`), `deck-test.sh` / `desktop-smoke.sh`, `controller-capture.py` (dumps changed report bits for new controllers)
* `.claude/`: shared Claude Code permissions and hooks (`hooks/guard.py` blocks edits to generated files, skipped hooks, unsigned commits, force pushes and secrets)
* `docs/`: troubleshooting, Steam Deck testing notes, adding a controller, maintainer playbook

## Rules agents break

* Run `make check` before every push. It runs everything CI runs on a PR except the container build (`make check-full` adds that). `make tools` installs the pinned linters into `.venv`, `make hooks` installs the git hooks.
* Third-party libraries are linked statically in release builds. Only system-dependent libraries (glibc, Vulkan, PipeWire, libsystemd, X11/Wayland client libraries) may be dynamic. Don't add shared-library dependencies.
* Must work on desktop Linux broadly and on the Steam Deck. Detect capabilities at runtime (sockets, devices, protocols); never hardcode a distro, device, hostname, user name or `/home/<user>` path.
* Never commit secrets, passwords, hostnames, IPs or personal paths. Use placeholders like `<deck-host>`.
* Don't edit generated or vendored files: `build/`, `dist/`, `decky/dist/`, `decky/pnpm-lock.yaml` (use pnpm), `LICENSES/` (verbatim license texts), `CHANGELOG.md` and `.release-please-manifest.json` (release-please owns them).
* Edit the Decky UI in `decky/src/index.tsx`, never `decky/dist/index.js`; run `make decky` and commit both. CI rebuilds the bundle and fails if it differs.
* The Decky plugin runs as the `deck` user, not root, and may never set `exec` actions or commands. Those stay config-file only.
* Pin everything: GitHub Actions to full commit SHAs with a version comment, FetchContent to a commit SHA or release tag.
* Comments are rare and explain why, not what. No trailing whitespace; newline at end of file; every new source file starts with the one-line SPDX MIT header the existing files use.
* New third-party code or assets go into `THIRD_PARTY_NOTICES.md` with their license text in `LICENSES/`.
* Notes, investigation logs and screenshots go in the PR, not in the repo. Agent instructions live only in this file.
* Sign off every commit (`git commit -s`); never skip hooks. PR titles are Conventional Commits. Keep PRs to one topic.
* PR body: copy [the template](.github/pull_request_template.md), keep every line, tick every box, fill `What`, `Tested`, `Tested on`, `Fixes` (#N or none) and `AI-assisted` (yes or no). The Conventions check enforces this.

## Definition of done

A change is done when all of these hold:

* `make check` passes locally, and `make check-full` for changes to the build, packaging, `scripts/install.sh` or the AppImage
* New pure logic has tests in `tests/` (registered in `tests/CMakeLists.txt`) or `decky/tests/`
* Behavior changes are reflected in `README.md`, `docs/`, the sample `config.yaml` and the Decky plugin
* Hardware-facing changes (`src/controllers*`, `src/buttons*`, `src/haptic*`, `src/overlay*`, `src/pedal*`, `src/keyboard*`, `decky/`, the service unit) say in the PR how they were tested on real hardware, or that they weren't
* The PR body follows the template and CI is green

## Known pitfalls and rejected approaches

Don't retry these without new information.

* **Valve controller input in Game Mode.** Steam owns the Deck and the Steam Controller, and hid-steam hides their evdev gamepad nodes while Steam holds hidraw, so evdev sees nothing from them. Decky's `SteamClient.Input.RegisterForControllerInputMessages` only fires while the Steam UI has focus and stops once a game runs. The working approach is reading raw HID reports from every Valve `/dev/hidraw*` node with a decoder in `src/buttons.cpp` (tested with byte fixtures in `tests/test_buttons.cpp`):
  * Deck (28DE:1205): 64-byte reports with `data[0] == 0x01` and `data[2] == 0x09`, buttons in a little-endian u64 at byte 8 (A bit 7, L5 bit 15, R5 bit 16, L4 bit 41, R4 bit 42). Only one of its hidraw nodes streams.
  * 2026 Steam Controller (1302 USB, 1303 BLE, 1304 puck, 1305 Steam Machine): state reports `0x42` (54 bytes on the puck), `0x45` (BLE) or `0x47`, buttons in a little-endian u32 at byte 2 (A bit 0, R4 bit 7, R5 bit 8, L4 bit 17, L5 bit 18). Bits 28 and 29 are grip touch sensors, not buttons. The puck has one hidraw interface per controller slot (interfaces 2 to 6); open all of them. Reports `0x43`, `0x46`, `0x79` and `0x7b` are battery and connection status.
  * Never read Valve devices over evdev as well: Steam's virtual pad (28DE:11FF) and hid-steam's own nodes would double every press.
  * An open hid-steam hidraw node counts as a client, and hid-steam then drops its evdev gamepad and lizard mode for that device. With Steam running that changes nothing; without Steam, other apps lose the kernel gamepad while Whisker holds it. That's why `Controllers` opens nothing when `trigger_button` is empty until a capture asks for it.
* **Button names** follow Steam's, by position: L4/R4 are the upper back buttons and L5/R5 the lower ones on the Deck and the Steam Controller (`button_back_left_upper` = L4, `button_back_left` = L5). A user who feels a mismatch is holding a different shell, not a different button.
* **Back paddles on other controllers** use `BTN_GRIPL`/`GRIPL2`/`GRIPR`/`GRIPR2` since Linux 6.17. xpad used `BTN_TRIGGER_HAPPY5-8` before that, which other drivers use for unrelated buttons, so that fallback is limited to Microsoft's vendor ID.
* **Hotplug** retries a node on `IN_ATTRIB` because udev sets permissions after the node appears.
* **Per-game Steam Input bindings** can steal the trigger button (Chiaki bound L5 to "end session"). Users must leave the button unbound per game.
* **Overlay under Gamescope.** Wayland layer-shell surfaces are never composited: they land in Gamescope's `externalOverlayWindow` slot, which MangoApp already holds via X11. The working approach is a full-width X11 window with the `STEAM_OVERLAY` and `GAMESCOPE_NO_FOCUS` atoms. Don't try layer-shell on Gamescope.
* **Overlay backend selection** happens per recording: a connectable `gamescope-0` (or `$GAMESCOPE_WAYLAND_DISPLAY`) socket in `$XDG_RUNTIME_DIR` selects the Gamescope backend on `$DISPLAY` (default `:0`); otherwise layer-shell on `$WAYLAND_DISPLAY`, then any other `wayland-*` socket; otherwise no overlay. Tested in `tests/test_overlay.cpp`.
* **Screenshots** through the XDG portal or Steam only capture the game plane; the overlay never shows in them. Check the journal (`overlay: selected gamescope backend`) instead.
* **Shutdown SIGBUS.** With the default `KillMode=control-group`, systemd killed the AppImage FUSE helper together with Whisker, unmapping the binary mid-shutdown. The unit must keep `KillMode=mixed` (enforced by the `service_kill_mode` ctest).
* **Decky config writes.** The plugin's YAML writer once dropped the nested `pedals:` block on every save. Keep the round-trip tests in `decky/tests/` passing when touching `parse_yaml`/`serialize_yaml`.
* **submit_key.** The global `submit_key` overrides a pedal's `suffix`; Enter is skipped when the text is empty or already ends in a newline (`effectiveSubmitKey`, tested).
* **Deck mic** can sit muted at volume 0, which shows up as `below noise threshold (rms=0.000000)`.
* **Desktop pedal** access needs the udev rule (`70-pedal-stt.rules`, installed as `70-whisker.rules`) tagging the Elgato pedal's hidraw/usb/input nodes with `uaccess`.
* **CPU baseline.** ggml defaults to `-march=native`, so a CI build once picked up the runner's AVX-512 and crashed with SIGILL at model load on Zen 2/3 (#13). Release builds keep `WHISKER_NATIVE=OFF` (x86-64-v3: AVX2, FMA, F16C, BMI2), and `scripts/check-cpu-baseline.sh` fails the build if the shipped binary uses AVX-512. `GGML_CPU_ALL_VARIANTS` would add runtime dispatch but requires `GGML_BACKEND_DL` and shared ggml libraries, which the static linking policy rules out.
* **Testing on hardware.** Use `scripts/deck-test.sh` / `make deck-test` with `DECK_HOST` set. Never run code from an unreviewed fork PR on real hardware.
