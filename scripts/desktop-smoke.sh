#!/bin/bash
# SPDX-License-Identifier: MIT
# Installs a build as the user service on this desktop and checks that it runs.
#
#   scripts/desktop-smoke.sh [Whisker-x86_64.AppImage]
set -euo pipefail

cd "$(dirname "$0")/.."

APPIMAGE="${1:-dist/Whisker-x86_64.AppImage}"
[ -f "$APPIMAGE" ] || ./build-container.sh
bash scripts/smoke-checks.sh "$APPIMAGE"
