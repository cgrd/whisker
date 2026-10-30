# Third-party notices

Whisker's own code is MIT licensed (see [LICENSE](LICENSE)). Release artifacts also contain the third-party components below. Full license texts are in [LICENSES/](LICENSES/), named by SPDX identifier. The AppImage additionally carries each component's upstream license file under `usr/share/doc/whisker/upstream/`.

## AppImage: statically linked into `usr/bin/whisker`

| Component | Version | License | Source |
|-----------|---------|---------|--------|
| transcribe.cpp | commit pinned in CMakeLists.txt | MIT | https://github.com/handy-computer/transcribe.cpp |
| ggml (vendored in transcribe.cpp) | see transcribe.cpp `ggml/UPSTREAM` | MIT | https://github.com/ggml-org/ggml |
| miniz (vendored in transcribe.cpp) | see transcribe.cpp | MIT | https://github.com/richgel999/miniz |
| Silero VAD (vendored in transcribe.cpp) | see transcribe.cpp | MIT | https://github.com/snakers4/silero-vad |
| Dear ImGui | 1.93.0 | MIT | https://github.com/ocornut/imgui |
| Monocypher (with the optional Ed25519 code) | 4.0.3 | BSD-2-Clause (chosen from BSD-2-Clause OR CC0-1.0) | https://github.com/LoupVaillant/Monocypher |
| SDL2 | 2.32.10 | Zlib | https://github.com/libsdl-org/SDL |
| cairo | 1.18.2 | LGPL-2.1-only OR MPL-1.1 | https://cairographics.org/releases/cairo-1.18.2.tar.xz |
| pixman | 0.43.4 | MIT | https://cairographics.org/releases/ |
| FreeType | 2.13.3 | FTL (chosen from FTL OR GPL-2.0-or-later) | https://freetype.org |
| libpng | 1.6.43 | libpng-2.0 | https://github.com/pnggroup/libpng |
| zlib | 1.3.1 | Zlib | https://zlib.net |
| Opus | 1.5.2 | BSD-3-Clause | https://opus-codec.org |
| libopusenc | 0.2.1 | BSD-3-Clause | https://opus-codec.org |
| libevdev | 1.13.3 | MIT | https://freedesktop.org/wiki/Software/libevdev/ |
| wlr-layer-shell protocol (generated code) | unstable v1 | HPND-sell-variant | https://gitlab.freedesktop.org/wlroots/wlr-protocols |
| xdg-shell protocol (generated code) | wayland-protocols 1.x | MIT | https://gitlab.freedesktop.org/wayland/wayland-protocols |
| Atkinson Hyperlegible font (embedded) | 1.006 | OFL-1.1 | https://www.brailleinstitute.org/freefont/ |
| Whisker icon (embedded, and `whisker.png` in the AppImage) | | CC0-1.0 | `assets/` |

Icon art generated with Google Gemini; no copyright claimed.

Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved.

## AppImage: bundled shared libraries (`usr/lib/`)

Taken from the Arch Linux Archive snapshot pinned in the Containerfile.

| Component | Version | License |
|-----------|---------|---------|
| libX11 | 1.8.10 | MIT AND X11 |
| libxcb | 1.17.0 | X11 |
| libXau | 1.0.12 | MIT-open-group |
| libXdmcp | 1.1.5 | MIT-open-group |
| libxkbcommon | 1.7.0 | MIT |
| libffi | 3.4.6 | MIT |
| libcap | 2.71 | BSD-3-Clause (chosen from BSD-3-Clause OR GPL-2.0-only) |
| libstdc++, libgcc_s (GCC) | 14.2.1 | GPL-3.0-or-later WITH GCC-exception-3.1 |

glibc, the Vulkan loader, libwayland, PipeWire, libsystemd, D-Bus and GPU drivers are not bundled. They come from the host system.

## AppImage runtime

The first part of the AppImage file is the [AppImage type2-runtime](https://github.com/AppImage/type2-runtime) (MIT), release 20251108 (commit dd6cebe), pinned by SHA-256 in `appimage/build-appimage.sh`. It statically links musl (MIT), libfuse 3.15.0 (LGPL-2.1-only), squashfuse (BSD-2-Clause), zstd (BSD-3-Clause, chosen from BSD-3-Clause OR GPL-2.0-only) and zlib (Zlib).

## Decky plugin (`Whisker-decky.zip`)

| Component | Version | License | Notes |
|-----------|---------|---------|-------|
| Whisker plugin (main.py, UI) | | MIT | |
| @decky/api | 1.1.3 | LGPL-2.1-only | bundled into `dist/index.js`, https://github.com/SteamDeckHomebrew/loader-api |
| react-icons | 5.7.0 | MIT | `GenIcon` helper bundled into `dist/index.js` |
| Font Awesome Free "microphone" icon | 5 | CC-BY-4.0 | via react-icons, by Fonticons, Inc., https://fontawesome.com |

@decky/ui and React are provided by Decky Loader at runtime and are not bundled.

## LGPL components

Three LGPL-2.1 components ship in release artifacts:

* cairo, statically linked into the `whisker` binary
* libfuse, statically linked into the AppImage runtime
* @decky/api, bundled into the Decky plugin's `dist/index.js`

LGPL-2.1 section 6 lets you modify these libraries and relink. To do that:

* Whisker's complete source and build scripts are public at https://github.com/cgrd/whisker under MIT. Change the cairo version or source in `Containerfile` and run `./build-container.sh` to produce an AppImage linked against your cairo.
* Every release has a `Whisker-lgpl-sources.tar` asset with the exact upstream sources of cairo, libfuse, the type2-runtime commit and @decky/api used for that release, downloaded from the same place as the binaries.
* The AppImage runtime can be swapped by rebuilding with appimagetool's `--runtime-file`.
* The Decky plugin UI is built from `decky/src/` with `make decky`; change the @decky/api version in `decky/package.json` (or point it at your own copy) and rebuild.

For cairo you may instead use it under MPL-1.1. Its source is the unmodified upstream release listed above.

## Downloaded at runtime, not distributed

| Component | License | Source |
|-----------|---------|--------|
| NVIDIA Parakeet unified-en 0.6B, GGUF conversion | NVIDIA Open Model License | https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf (from https://huggingface.co/nvidia/parakeet-unified-en-0.6b) |

Whisker's settings window, the Decky plugin and `get-model.sh` download the model from Hugging Face only when the user picks one; the install script never downloads it. Downloads are pinned to the repo commit in `models.json` and verified against the SHA-256 and size recorded there. It's governed by the [NVIDIA Open Model License Agreement](https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license/), not by Whisker's license.

## Build tools (not shipped)

appimagetool 1.9.1, CMake, Meson, GCC, glslang/shaderc and the Arch Linux packages in the build container are used to build releases but are not part of them, apart from the libraries listed above.
