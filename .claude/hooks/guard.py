#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Claude Code hooks for this repo. Exit code 2 blocks the tool call and shows stderr to the agent."""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("CLAUDE_PROJECT_DIR", Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(ROOT / "scripts"))

from check_conventions import file_problems, line_problems  # noqa: E402

GENERATED = (
    "build/", "dist/", ".venv/", "Whisker.AppDir/", "third-party-licenses/", "LICENSES/",
    "CHANGELOG.md", ".release-please-manifest.json", "decky/dist/", "decky/node_modules/",
    "decky/pnpm-lock.yaml",
)
BLOCKED_COMMANDS = [
    (re.compile(r"\bgit\b[^;&|]*\s--no-verify\b|\bgit\s+commit\b[^;&|]*\s-[a-zA-Z]*n\b"),
     "Don't skip git hooks. Fix what they report."),
    (re.compile(r"\bgit\s+push\b[^;&|]*\s(--force(?!-with-lease)\b|-[a-zA-Z]*f\b)"),
     "Plain force pushes are blocked; use --force-with-lease on your own branch."),
    (re.compile(r"\bgit\s+push\b[^;&|]*\s(origin\s+)?(HEAD:)?(main|master)\b"),
     "Don't push to main. Push a branch and open a pull request."),
    (re.compile(r"\b(curl|wget)\b[^;&]*\|\s*(sudo\s+)?(ba|z)?sh\b"),
     "Don't pipe downloads into a shell. Download, read, then run."),
]


def block(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def relative(path):
    try:
        return Path(path).resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return None


def pre_edit(tool_input):
    path = relative(tool_input.get("file_path") or tool_input.get("notebook_path") or "")
    if path and path.startswith(GENERATED):
        block(f"{path} is generated or vendored; change its source instead (see AGENTS.md).")


def post_edit(tool_input):
    path = relative(tool_input.get("file_path") or "")
    if not path or path.startswith(GENERATED) or not (ROOT / path).is_file():
        return
    try:
        content = (ROOT / path).read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return
    problems = [f"{path}:{n}: [{rule}] {detail}" for n, rule, detail in file_problems(path, content)]
    for number, text in enumerate(content.split("\n"), 1):
        problems += [f"{path}:{number}: [{rule}] {detail}" for rule, detail in line_problems(path, text)]
    linters = []
    if path.endswith(".sh") or path in ("appimage/AppRun",) or path.startswith(".githooks/"):
        linters.append(["shellcheck", "-S", "warning", path])
    if path.endswith(".py"):
        linters.append(["ruff", "check", "--quiet", path])
    for command in linters:
        if shutil.which(command[0]) is None:
            continue
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            problems.append(result.stdout.strip() or result.stderr.strip())
    if problems:
        block("Fix before moving on:\n" + "\n".join(problems))


def pre_bash(tool_input):
    command = tool_input.get("command", "")
    for pattern, message in BLOCKED_COMMANDS:
        if pattern.search(command):
            block(message)
    if re.search(r"\bgit\s+commit\b", command):
        if not re.search(r"\s(-[a-zA-Z]*s[a-zA-Z]*|--signoff)\b", command):
            block("Commits need a DCO sign-off: use git commit -s.")
        result = subprocess.run([sys.executable, "-I", "scripts/check_conventions.py", "--staged"],
                                cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            block("Staged changes break the conventions:\n" + result.stdout)


def main():
    event = json.load(sys.stdin)
    handlers = {"pre-edit": pre_edit, "post-edit": post_edit, "pre-bash": pre_bash}
    handlers[sys.argv[1]](event.get("tool_input", {}))


if __name__ == "__main__":
    main()
