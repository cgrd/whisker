# SPDX-License-Identifier: MIT
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from check_conventions import (  # noqa: E402
    doc_link_problems,
    file_problems,
    line_problems,
    new_file_problems,
    pr_problems,
)

GOOD_BODY = """### What
Fix a thing.

### Tested
make test on Arch.

### Checklist
* [x] Tested on: KDE Plasma, Arch
* [x] Fixes: none
* [x] AI-assisted: no
"""


def rules(problems):
    return [p[-2] for p in problems]


@pytest.mark.parametrize("text", [
    "password: hunter22",
    "echo hunter2 | sudo -S true",
    "ssh deck@steamdeck.tail1234.ts.net",
    "cd /home/alice/code",
    "host 192.168.1.20",
    "token ghp_" + "a" * 36,
    "-----BEGIN OPENSSH PRIVATE KEY-----",
])
def test_secrets_are_flagged(text):
    assert "secret" in rules(line_problems("docs/x.md", text))


@pytest.mark.parametrize("text", [
    "rm -rf /home/deck/homebrew/plugins/Whisker",
    "ssh deck@<deck-host>",
    "It asks for your password once.",
    "version 10.2.0",
])
def test_ordinary_text_is_not_a_secret(text):
    assert "secret" not in rules(line_problems("docs/x.md", text))


def test_scanner_files_may_contain_patterns():
    assert line_problems("scripts/tests/test_check_conventions.py", "password: hunter22") == []


def test_actions_must_be_pinned_to_a_sha():
    path = ".github/workflows/ci.yml"
    assert rules(line_problems(path, "      - uses: actions/checkout@v7")) == ["unpinned-action"]
    assert line_problems(path, "      - uses: actions/checkout@" + "a" * 40 + " # v7") == []
    assert line_problems(path, "    uses: ./.github/workflows/build.yml") == []


def test_fetchcontent_must_not_track_a_branch():
    assert rules(line_problems("CMakeLists.txt", "    GIT_TAG main")) == ["unpinned-dependency"]
    assert line_problems("CMakeLists.txt", "    GIT_TAG ${IMGUI_TAG}") == []
    assert line_problems("CMakeLists.txt", 'set(IMGUI_TAG v1.91.8 CACHE STRING "x")') == []


def test_trailing_whitespace():
    assert rules(line_problems("src/a.cpp", "int x; ")) == ["whitespace"]


def test_spdx_header_and_newline():
    assert rules(file_problems("src/a.cpp", "int x;")) == ["eof-newline", "spdx"]
    assert file_problems("src/a.cpp", "// SPDX-License-Identifier: MIT\nint x;\n") == []
    assert file_problems("scripts/x.sh", "#!/bin/bash\n# SPDX-License-Identifier: MIT\n") == []


def test_doc_style():
    found = rules(file_problems("docs/a.md", "a — b\n\n---\n\n- item\n"))
    assert found == ["doc-style", "doc-style", "doc-style"]
    assert file_problems("docs/a.md", "```\n- in a code block\n```\n* item\n") == []
    assert file_problems("CODE_OF_CONDUCT.md", "a — b\n") == []


def test_doc_links_resolve():
    exists = {"docs/MAINTAINING.md", "README.md"}.__contains__
    assert doc_link_problems("README.md", "[m](docs/MAINTAINING.md)\n", exists) == []
    assert doc_link_problems("docs/x.md", "[r](../README.md#top)\n", exists) == []
    assert rules(doc_link_problems("README.md", "[x](missing.md)\n", exists)) == ["doc-link"]
    assert doc_link_problems("README.md", "[w](https://example.com)\n", exists) == []


def test_new_files():
    assert rules(new_file_problems("NOTES.md")) == ["notes-file"]
    assert rules(new_file_problems("docs/GEMINI.md")) == ["agent-file"]
    assert rules(new_file_problems("src/.cursorrules")) == ["agent-file"]
    assert rules(new_file_problems("docs/AGENTS.md")) == ["agent-file"]
    assert new_file_problems("AGENTS.md") == []
    assert new_file_problems("docs/troubleshooting.md") == []


def test_pull_request_template():
    assert pr_problems("fix(deckpad): ignore short reports", GOOD_BODY) == []
    assert rules(pr_problems("Fix stuff", GOOD_BODY)) == ["pr-title"]
    found = [p[1] for p in pr_problems("fix: x", GOOD_BODY.replace("[x] AI-assisted: no", "[ ] AI-assisted:"))]
    assert "unchecked: AI-assisted:" in found
    assert "fill in 'AI-assisted:'" in found
    assert rules(pr_problems("fix: x", "### What\n\n### Tested\nyes\n### Checklist\n")) == \
        ["pr-template", "pr-template"]
