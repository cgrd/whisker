#!/bin/bash
# SPDX-License-Identifier: MIT
# Runs `reuse lint` but allows unused license texts: LICENSES/ also holds the
# licenses of libraries that only exist inside the release AppImage.
set -euo pipefail

cd "$(dirname "$0")/.."

# reuse exits non-zero for unused licenses too; the filter below decides
{ reuse lint --json || true; } | python3 -c '
import json, sys
report = json.load(sys.stdin)
problems = {k: v for k, v in report["non_compliant"].items() if v and k != "unused_licenses"}
for kind, items in problems.items():
    print(f"{kind}: {items}")
if problems:
    sys.exit("REUSE lint failed; run `reuse lint` for details")
print("REUSE lint passed")
'
