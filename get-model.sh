#!/bin/bash
# SPDX-License-Identifier: MIT
# Downloads the speech model you name. Without an argument it lists the models and downloads nothing.
#   bash get-model.sh Q8_0
set -euo pipefail

TABLE="${WHISKER_MODELS_JSON:-$(dirname "$0")/models.json}"
DEST="$HOME/.local/share/pedal-stt/models"
VERSION="1.1.1" # x-release-please-version

# model_field <quant> <file|url|size|sha256>
model_field() {
    python3 -I -c '
import json, sys
table = json.load(open(sys.argv[1]))
for m in table["models"]:
    if m["quant"].lower() == sys.argv[2].lower():
        if sys.argv[3] == "url":
            print(table["repo"] + "/resolve/" + table["revision"] + "/" + m["file"])
        else:
            print(m[sys.argv[3]])
        break
else:
    sys.exit(1)
' "$TABLE" "$1" "$2"
}

list_models() {
    python3 -I -c '
import json, sys
table = json.load(open(sys.argv[1]))
for m in table["models"]:
    tag = " (recommended)" if m["file"] == table["recommended"] else ""
    size = (m["size"] + 500000) // 1000000
    print("  %-8s %d-bit, %d MB. %s%s" % (m["quant"], m["bits"], size, m["note"], tag))
' "$TABLE"
}

if [ $# -ne 1 ] || ! model_field "$1" file >/dev/null; then
    echo "usage: $0 <model>, one of:" >&2
    list_models >&2
    exit 2
fi

file=$(model_field "$1" file)
tmp="$DEST/.$file.part"
mkdir -p "$DEST"
# One retry on a transient error, never a loop: run the script again to retry
if ! curl -fL --retry 1 -A "Whisker/$VERSION (+https://github.com/cgrd/whisker)" -o "$tmp" "$(model_field "$1" url)"; then
    rm -f "$tmp"
    echo "error: download failed; run this script again to retry" >&2
    exit 1
fi
if [ "$(stat -c %s "$tmp")" != "$(model_field "$1" size)" ] \
        || [ "$(sha256sum "$tmp" | cut -d' ' -f1)" != "$(model_field "$1" sha256)" ]; then
    rm -f "$tmp"
    echo "error: the download doesn't match the expected size and SHA-256, so it was deleted" >&2
    exit 1
fi
mv "$tmp" "$DEST/$file"
echo "Downloaded and verified $DEST/$file. Select it in Whisker settings, or set model: in ~/.config/pedal-stt/config.yaml"
