# SPDX-License-Identifier: MIT
PREFIX ?= $(HOME)/.local
BUILD_DIR := build
CMAKE_ARGS ?=
BASE ?= $(shell git rev-parse --verify -q origin/main >/dev/null && echo origin/main || echo main)
SHELL_SCRIPTS := $(wildcard *.sh scripts/*.sh appimage/*.sh) appimage/AppRun $(wildcard .githooks/*)
export PATH := $(CURDIR)/.venv/bin:$(PATH)

.PHONY: all test test-cpp test-python lint conventions decky decky-check check check-full tools hooks \
        deck-test desktop-smoke hil-test install uninstall enable clean

all:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) -j$$(nproc)

test: test-cpp test-python

test-cpp: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-python:
	python3 -m pytest -q decky/tests scripts/tests

lint:
	shellcheck -S warning $(SHELL_SCRIPTS)
	ruff check decky scripts .claude
	actionlint
	zizmor --offline --min-severity medium .github/workflows
	scripts/reuse-lint.sh

decky:
	cd decky && pnpm install --frozen-lockfile && pnpm build

# The committed bundle must be exactly what the TypeScript source builds to
decky-check:
	mkdir -p $(BUILD_DIR)
	cp decky/dist/index.js $(BUILD_DIR)/committed-index.js
	$(MAKE) decky
	cmp decky/dist/index.js $(BUILD_DIR)/committed-index.js || \
		{ cp $(BUILD_DIR)/committed-index.js decky/dist/index.js; echo "decky/dist/index.js is stale: run 'make decky' and commit it"; exit 1; }

conventions:
	python3 -I scripts/check_conventions.py --base $(BASE) --worktree

# Everything CI runs on a PR except the container build and install tests
check: test lint conventions decky-check

# Adds the release container build and the install script test, like CI
check-full: check
	./build-container.sh
	scripts/check-cpu-baseline.sh dist/Whisker-x86_64.AppImage
	podman run --rm -v "$(CURDIR):/src:ro" -e WHISKER_RELEASE_URL=file:///src/dist archlinux:base \
		bash -c 'pacman -Sy --noconfirm --needed curl python libpipewire vulkan-icd-loader wayland >/dev/null && /src/scripts/test-install.sh'

tools:
	python3 -m venv .venv
	.venv/bin/pip install --quiet -r scripts/requirements-dev.txt

hooks:
	git config core.hooksPath .githooks

deck-test:
	scripts/deck-test.sh

desktop-smoke:
	scripts/desktop-smoke.sh

# Virtual controllers against a private Whisker instance; needs /dev/uhid and /dev/uinput
# (SUDO= if you already have access). HIL_ARGS=--no-uhid skips the fake Steam Controller,
# which a running Steam would pick up.
SUDO ?= sudo
hil-test: all
	$(SUDO) python3 -I scripts/hil-test.py --whisker $(BUILD_DIR)/whisker $(HIL_ARGS)

install: all
	cmake --install $(BUILD_DIR)
	pkexec bash -c 'install -Dm644 $(CURDIR)/70-pedal-stt.rules /etc/udev/rules.d/70-whisker.rules && udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw'

enable: install
	systemctl --user daemon-reload
	systemctl --user enable --now whisker

uninstall:
	systemctl --user disable --now whisker 2>/dev/null || true
	rm -f $(PREFIX)/bin/whisker
	rm -f $(PREFIX)/share/applications/whisker.desktop
	rm -f $(PREFIX)/share/icons/hicolor/256x256/apps/whisker.png
	rm -f $(PREFIX)/lib/systemd/user/whisker.service
	pkexec bash -c 'rm -f /etc/udev/rules.d/70-whisker.rules && udevadm control --reload-rules'
	systemctl --user daemon-reload

clean:
	rm -rf $(BUILD_DIR)
