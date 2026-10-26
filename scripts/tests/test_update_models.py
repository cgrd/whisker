# SPDX-License-Identifier: MIT
import importlib.util
import json
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "update-models.py"
spec = importlib.util.spec_from_file_location("update_models", SCRIPT)
update_models = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update_models)


def table():
    return json.loads(update_models.TABLE.read_text())


def tree_for(models, size=1, oid="a" * 64):
    return [{"path": "README.md", "size": 10}] + [
        {"path": m["file"], "size": size, "lfs": {"size": size, "oid": oid}} for m in models
    ]


def test_apply_tree_pins_revision_sizes_and_hashes():
    t = table()
    updated = update_models.apply_tree(t, "b" * 40, tree_for(t["models"], 42))
    assert updated["revision"] == "b" * 40
    assert all(m["size"] == 42 and m["sha256"] == "a" * 64 for m in updated["models"])


def test_apply_tree_fails_when_a_file_is_gone():
    t = table()
    with pytest.raises(SystemExit):
        update_models.apply_tree(t, "b" * 40, tree_for(t["models"][1:]))


def test_current_table_matches_its_own_format():
    assert json.dumps(table(), indent=2) + "\n" == update_models.TABLE.read_text()
