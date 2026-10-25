#!/bin/bash
# SPDX-License-Identifier: MIT
RULES_SRC="$1"
install -Dm644 "$RULES_SRC" /etc/udev/rules.d/70-whisker.rules
udevadm control --reload-rules
