#!/bin/bash
# SPDX-License-Identifier: MIT
# Lists changes in BASE...HEAD that deserve a closer look during review.
#
#   scripts/review-flags.sh origin/main HEAD
set -euo pipefail

BASE="${1:?usage: review-flags.sh <base> <head>}"
HEAD="${2:?usage: review-flags.sh <base> <head>}"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/stdout}"
RANGE="$BASE...$HEAD"

flags=()
flag() {
    flags+=("$1")
    echo "::warning::$1"
}

while read -r file; do
    case "$file" in
        .github/*|scripts/*|appimage/*|Containerfile|build-container.sh|decky/main.py|decky/dist/index.js|release-please-config.json|REUSE.toml)
            flag "sensitive file changed: \`$file\`" ;;
    esac
done < <(git diff --name-only "$RANGE")

if git diff "$RANGE" -- CMakeLists.txt | grep -qE '^\+.*(GIT_REPOSITORY|GIT_TAG|_TAG |URL)'; then
    flag "CMake dependency source or pin changed"
fi

while read -r file; do
    flag "binary file added or changed: \`$file\`"
done < <(git diff --numstat "$RANGE" | awk '$1 == "-" { print $3 }')

added_lines() {
    git diff -U0 "$RANGE" -- "$@" | grep -E '^\+[^+]' || true
}

while read -r domain; do
    [ -z "$domain" ] && continue
    git grep -qF "$domain" "$BASE" -- . || flag "new domain referenced: \`$domain\`"
done < <(added_lines . | grep -oE 'https?://[A-Za-z0-9.-]+' | sed 's|.*://||' | sort -u)

if added_lines . | grep -qE '[A-Za-z0-9+/]{120,}={0,2}'; then
    flag "long base64-like blob added"
fi

if added_lines '*.sh' Containerfile Makefile '.github/**' | grep -qE '\b(curl|wget)\b'; then
    flag "new curl/wget call in a script, Containerfile or workflow"
fi

if added_lines . | grep -qE '\b(eval|base64 -d|xxd -r)\b|\|[[:space:]]*(ba)?sh\b'; then
    flag "eval, decoding or piping into a shell added"
fi

while read -r file; do
    case "$file" in
        *.lock|*lock.json|*lock.yaml|LICENSES/*|*.svg|decky/dist/*) continue ;;
    esac
    if git diff -U0 "$RANGE" -- "$file" | grep -E '^\+[^+]' | awk 'length > 500 { found = 1 } END { exit !found }'; then
        flag "very long lines (minified or generated code?) in \`$file\`"
    fi
done < <(git diff --name-only "$RANGE")

if git diff --name-only "$RANGE" | grep -q '^decky/pnpm-lock.yaml$'; then
    flag "Decky dependencies changed: check \`decky/pnpm-lock.yaml\` for new or renamed packages"
fi

{
    echo "## Review flags"
    if [ ${#flags[@]} -eq 0 ]; then
        echo "Nothing unusual."
    else
        echo "Look at these closely before approving or running this PR's code:"
        echo
        printf '* %s\n' "${flags[@]}"
    fi
} >> "$SUMMARY"

