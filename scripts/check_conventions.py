#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Checks a change against the rules in CONTRIBUTING.md and AGENTS.md.

  scripts/check_conventions.py --base origin/main               # committed work
  scripts/check_conventions.py --base origin/main --worktree    # plus uncommitted and untracked files
  scripts/check_conventions.py --staged                         # pre-commit hook
  scripts/check_conventions.py --base B --head H --pr-title T --pr-body-file F   # CI
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT_DOCS = {
    "README.md", "CONTRIBUTING.md", "CODE_OF_CONDUCT.md", "SECURITY.md", "THIRD_PARTY_NOTICES.md",
    "AGENTS.md", "CLAUDE.md", "CHANGELOG.md",
}
AGENT_FILE = re.compile(
    r"(^|/)(gemini\.md|copilot-instructions\.md|\.cursorrules|\.windsurfrules|\.clinerules|"
    r"chatgpt\.md|\.aider\.conf\.ya?ml|agents\.md|claude\.md)$",
    re.I,
)
BINARY_DIRS = ("assets/", "fonts/")
SPDX_SUFFIXES = {".cpp", ".h", ".py", ".sh"}
SPDX_PATHS = {"appimage/AppRun", "Makefile", "Containerfile"}
STYLE_EXEMPT_DOCS = ("LICENSES/", "CODE_OF_CONDUCT.md", "CHANGELOG.md")
SCANNER_FILES = ("scripts/check_conventions.py", "scripts/tests/", ".claude/hooks/")

SECRETS = [
    ("private key", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("GitHub token", re.compile(r"\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}|\bgithub_pat_[A-Za-z0-9_]{40,}")),
    ("AWS key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("Slack token", re.compile(r"\bxox[abpors]-[A-Za-z0-9-]{10,}")),
    ("password", re.compile(r"(?i)\b(password|passwd|pwd)\b\s*[:=]\s*[`'\"]?[^\s`'\"<>]{3,}")),
    ("password piped to sudo", re.compile(r"\becho\s+\S+\s*\|\s*sudo\s+-S\b")),
    ("tailnet hostname", re.compile(r"\b[\w-]+\.ts\.net\b")),
    ("personal home path", re.compile(r"/home/(?!deck\b|<)[a-z_][a-z0-9_-]*/")),
    ("private IP address", re.compile(r"\b(10\.\d{1,3}|192\.168|172\.(1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")),
]
USES = re.compile(r"^\s*-?\s*uses:\s*([^\s#]+)")
FULL_SHA = re.compile(r"[0-9a-f]{40}")
CMAKE_PIN = re.compile(r"^\s*(?:set\(\w+_TAG\s+|GIT_TAG\s+)([^\s)]+)")
CONVENTIONAL = re.compile(r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-z0-9._/-]+\))?!?: \S")
SECTION = re.compile(r"^###\s+(.+?)\s*$", re.M)
MD_LINK = re.compile(r"\]\(([^)\s]+)\)")


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def line_problems(path, text):
    problems = []
    if text != text.rstrip() and not path.startswith("LICENSES/"):
        problems.append(("whitespace", "trailing whitespace"))
    if not path.startswith(SCANNER_FILES):
        for name, pattern in SECRETS:
            if pattern.search(text):
                problems.append(("secret", name))
    if re.fullmatch(r"\.github/workflows/[^/]+\.ya?ml", path):
        match = USES.match(text)
        if match and not match[1].startswith("./"):
            ref = match[1].rpartition("@")[2]
            if not FULL_SHA.fullmatch(ref):
                problems.append(("unpinned-action", f"{match[1]} must be pinned to a full commit SHA"))
    if PurePosixPath(path).name == "CMakeLists.txt":
        match = CMAKE_PIN.match(text)
        if match and match[1] in ("main", "master", "HEAD"):
            problems.append(("unpinned-dependency", f"pin to a commit SHA or release tag, not {match[1]}"))
    return problems


def doc_style_problems(path, content):
    if not path.endswith(".md") or path.startswith(STYLE_EXEMPT_DOCS):
        return []
    problems = []
    fenced = False
    for number, text in enumerate(content.split("\n"), 1):
        if text.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        if "—" in text:
            problems.append((number, "doc-style", "em-dash; use a period, comma or colon"))
        if re.fullmatch(r"\s*(-{3,}|\*{3,}|_{3,})\s*", text):
            problems.append((number, "doc-style", "horizontal rule"))
        if re.match(r"\s*- ", text):
            problems.append((number, "doc-style", "use '*' for bullets"))
    return problems


def doc_link_problems(path, content, exists):
    if not path.endswith(".md") or path.startswith("LICENSES/"):
        return []
    problems = []
    fenced = False
    for number, text in enumerate(content.split("\n"), 1):
        if text.lstrip().startswith("```"):
            fenced = not fenced
        if fenced:
            continue
        for target in MD_LINK.findall(text):
            if re.match(r"[a-z]+:", target) or target.startswith("#"):
                continue
            resolved = os.path.normpath((PurePosixPath(path).parent / target.split("#")[0]).as_posix())
            if not exists(resolved):
                problems.append((number, "doc-link", f"{target} does not exist"))
    return problems


def file_problems(path, content):
    problems = []
    if content and not content.endswith("\n"):
        problems.append((0, "eof-newline", "file must end with a newline"))
    suffix = PurePosixPath(path).suffix
    if (suffix in SPDX_SUFFIXES or path in SPDX_PATHS or path.startswith(".githooks/")) \
            and "SPDX-License-Identifier:" not in "\n".join(content.split("\n")[:5]):
        problems.append((1, "spdx", "add an SPDX-License-Identifier: MIT header"))
    return problems + doc_style_problems(path, content)


def new_file_problems(path):
    if AGENT_FILE.search(path) and path not in ("AGENTS.md", "CLAUDE.md"):
        return [("agent-file", "agent instructions live in AGENTS.md only")]
    if path.endswith(".md") and "/" not in path and path not in ROOT_DOCS:
        return [("notes-file", "notes and reports go in the PR, not the repo; docs go in docs/")]
    return []


def pr_problems(title, body):
    problems = []
    if not CONVENTIONAL.match(title):
        problems.append(("pr-title", f"'{title}' is not a Conventional Commit title"))
    body = re.sub(r"<!--.*?-->", "", body or "", flags=re.S)
    parts = SECTION.split(body)
    sections = dict(zip(parts[1::2], parts[2::2], strict=True))
    for name in ("What", "Tested", "Checklist"):
        if not sections.get(name, "").strip():
            problems.append(("pr-template", f"section '### {name}' is missing or empty"))
    checklist = sections.get("Checklist", "")
    for item in re.findall(r"^\s*[-*]\s*\[ \]\s*(.+)$", checklist, re.M):
        problems.append(("pr-template", f"unchecked: {item.strip()}"))
    required = {
        "Fixes": r"Fixes:\s*(#\d+|none)\b",
        "Tested on": r"Tested on:\s*\S",
        "AI-assisted": r"AI-assisted:\s*(yes|no)\b",
    }
    for name, pattern in required.items():
        if checklist and not re.search(pattern, checklist, re.I):
            problems.append(("pr-template", f"fill in '{name}:'"))
    return problems


class Change:
    def __init__(self, args):
        if args.staged:
            self.diff_args = ["--cached"]
            self.show = lambda path: git("show", f":{path}")
            self.exists = lambda path: bool(git("ls-files", "--cached", "--", path).strip()) or Path(path).is_dir()
        elif args.worktree:
            self.diff_args = [args.base]
            self.show = lambda path: Path(path).read_text(encoding="utf-8")
            self.exists = lambda path: Path(path).exists()
        else:
            self.diff_args = [f"{args.base}...{args.head}"]
            head = args.head
            self.show = lambda path: git("show", f"{head}:{path}")
            tree = set(git("ls-tree", "-r", "--name-only", "-t", head).split("\n"))
            self.exists = lambda path: path.rstrip("/") in tree
        self.status = {}
        for line in git("diff", "--name-status", "--no-renames", *self.diff_args).splitlines():
            state, path = line.split("\t", 1)
            self.status[path] = state
        self.binary = {line.split("\t")[2] for line in git("diff", "--numstat", "--no-renames", *self.diff_args)
                       .splitlines() if line.startswith("-\t")}
        self.added = {}
        path = None
        for line in git("diff", "-U0", "--no-renames", "--no-color", *self.diff_args).splitlines():
            if line.startswith("+++ "):
                path = line[6:] if line.startswith("+++ b/") else None
            elif line.startswith("@@"):
                number = int(re.search(r"\+(\d+)", line)[1])
            elif path and line.startswith("+"):
                self.added.setdefault(path, []).append((number, line[1:]))
                number += 1
        if args.worktree:
            for path in git("ls-files", "--others", "--exclude-standard").splitlines():
                self.status[path] = "A"
                if not self.is_text(path):
                    self.binary.add(path)
                    continue
                content = Path(path).read_text(encoding="utf-8")
                self.added[path] = list(enumerate(content.split("\n"), 1))

    @staticmethod
    def is_text(path):
        try:
            Path(path).read_text(encoding="utf-8")
        except (UnicodeDecodeError, IsADirectoryError):
            return False
        return True


def check(change):
    problems = []
    for path, state in sorted(change.status.items()):
        if state == "D":
            continue
        if state == "A":
            problems += [(path, 0, rule, detail) for rule, detail in new_file_problems(path)]
            if re.fullmatch(r"tests/test_\w+\.cpp", path) and \
                    f"whisker_test({PurePosixPath(path).stem}" not in change.show("tests/CMakeLists.txt"):
                problems.append((path, 0, "test-registration", "add whisker_test() in tests/CMakeLists.txt"))
        if path in change.binary:
            if not path.startswith(BINARY_DIRS):
                problems.append((path, 0, "binary", "binary files belong in assets/ or fonts/ only"))
            continue
        try:
            content = change.show(path)
        except UnicodeDecodeError:
            problems.append((path, 0, "utf8", "text files must be UTF-8"))
            continue
        problems += [(path, number, rule, detail) for number, rule, detail in file_problems(path, content)]
        problems += [(path, number, rule, detail) for number, rule, detail in
                     doc_link_problems(path, content, change.exists)]
        for number, text in change.added.get(path, []):
            problems += [(path, number, rule, detail) for rule, detail in line_problems(path, text)]
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--staged", action="store_true")
    parser.add_argument("--worktree", action="store_true")
    parser.add_argument("--pr-title")
    parser.add_argument("--pr-body-file")
    args = parser.parse_args()

    problems = check(Change(args))
    if args.pr_title is not None:
        body = Path(args.pr_body_file).read_text(encoding="utf-8") if args.pr_body_file else ""
        found = pr_problems(args.pr_title, body)
        problems += [("pull request", 0, rule, detail) for rule, detail in found]

    for path, number, rule, detail in problems:
        location = f"{path}:{number}" if number else path
        print(f"{location}: [{rule}] {detail}")
    if problems:
        print(f"\n{len(problems)} problem(s). Rules: CONTRIBUTING.md and AGENTS.md")
        return 1
    print("Conventions check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
