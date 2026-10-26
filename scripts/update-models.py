#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pins models.json to the latest commit of the Hugging Face model repo, with each file's size and SHA-256.

  scripts/update-models.py              # latest commit
  scripts/update-models.py --revision SHA
"""
import argparse
import json
import urllib.request
from pathlib import Path

TABLE = Path(__file__).resolve().parent.parent / "models.json"
USER_AGENT = "Whisker deps-bump (+https://github.com/cgrd/whisker)"


def api(repo_url: str, path: str):
    repo = repo_url.removeprefix("https://huggingface.co/")
    request = urllib.request.Request(f"https://huggingface.co/api/models/{repo}{path}",
                                     headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def apply_tree(table: dict, revision: str, tree: list) -> dict:
    files = {entry["path"]: entry for entry in tree}
    for model in table["models"]:
        entry = files.get(model["file"])
        if not entry or "lfs" not in entry:
            raise SystemExit(f"{model['file']} is missing from revision {revision}")
        model["size"] = entry["lfs"]["size"]
        model["sha256"] = entry["lfs"]["oid"]
    table["revision"] = revision
    return table


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision")
    args = parser.parse_args()
    table = json.loads(TABLE.read_text())
    revision = args.revision or api(table["repo"], "")["sha"]
    tree = api(table["repo"], f"/tree/{revision}")
    TABLE.write_text(json.dumps(apply_tree(table, revision, tree), indent=2) + "\n")
    print(f"models.json pinned to {revision}")


if __name__ == "__main__":
    main()
