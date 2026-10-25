#!/bin/bash
# SPDX-License-Identifier: MIT
# Fails if a non-merge commit in BASE..HEAD lacks a Signed-off-by trailer for its author.
#
#   scripts/check-dco.sh origin/main HEAD
set -euo pipefail

BASE="${1:?usage: check-dco.sh <base> <head>}"
HEAD="${2:?usage: check-dco.sh <base> <head>}"

missing=0
for sha in $(git rev-list --no-merges "$BASE..$HEAD"); do
    email=$(git show -s --format=%ae "$sha")
    case "$email" in
        *"[bot]@users.noreply.github.com") continue ;;
    esac
    if ! git show -s --format='%(trailers:key=Signed-off-by,valueonly)' "$sha" | grep -qiF "<$email>"; then
        echo "::error::$(git show -s --format='%h %s' "$sha") is missing 'Signed-off-by: ... <$email>'"
        missing=$((missing + 1))
    fi
done

if [ "$missing" -gt 0 ]; then
    echo "$missing commit(s) lack a DCO sign-off. Fix with: git rebase --signoff $BASE && git push --force-with-lease"
    exit 1
fi
echo "All commits are signed off"
