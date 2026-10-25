# SPDX-License-Identifier: MIT
import sys
import types
from pathlib import Path

sys.modules.setdefault("decky", types.ModuleType("decky"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
