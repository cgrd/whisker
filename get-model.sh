#!/bin/bash
# SPDX-License-Identifier: MIT
# Downloads the speech model you name. Without an argument it lists the models and downloads nothing.
#   bash get-model.sh Q8_0
set -euo pipefail

TABLE="$(dirname "$0")/models.json"
DEST="$HOME/.local/share/pedal-stt/models"

# model_field <quant> <file|url>
model_field() {
    python3 -I -c '
import json, sys
table = json.load(open(sys.argv[1]))
for m in table["models"]:
    if m["quant"].lower() == sys.argv[2].lower():
        print(table["repo"] + "/resolve/main/" + m["file"] if sys.argv[3] == "url" else m[sys.argv[3]])
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
mkdir -p "$DEST"
curl -fL -o "$DEST/.$file.part" "$(model_field "$1" url)"
mv "$DEST/.$file.part" "$DEST/$file"
echo "Downloaded $DEST/$file. Select it in Whisker settings, or set model: in ~/.config/pedal-stt/config.yaml"
