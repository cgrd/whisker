#!/bin/bash
# SPDX-License-Identifier: MIT
# Builds the AppImage in the pinned Arch container and packages all release assets into dist/.
set -euo pipefail

cd "$(dirname "$0")"

mkdir -p dist
podman build -t whisker-build -f Containerfile .
podman run --rm -v "$(pwd)/dist:/output" whisker-build
scripts/package-release.sh dist
