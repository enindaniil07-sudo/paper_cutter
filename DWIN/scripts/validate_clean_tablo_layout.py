# -*- coding: utf-8 -*-
"""Validate CLEAN_TABLO design/layout.json."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REQUIRED = (
    "target_display",
    "target_touch",
    "travel_display",
    "btn_reset",
    "btn_stop",
    "btn_settings",
    "settings_title",
    "btn_settings_back",
    "set_row_brake",
    "set_val_brake",
    "set_row_on",
    "set_val_on",
    "set_row_off",
    "set_val_off",
    "set_row_spd",
    "set_val_spd",
    "set_edit_display",
    "set_edit_0",
    "set_edit_ok",
    "set_edit_cancel",
    "kb_display",
    "kb_1",
    "kb_2",
    "kb_3",
    "kb_4",
    "kb_5",
    "kb_6",
    "kb_7",
    "kb_8",
    "kb_9",
    "kb_0",
    "kb_del",
    "kb_ok",
    "kb_cancel",
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    path = args.project / "design" / "layout.json"
    if not path.is_file():
        print("ERROR: missing", path, file=sys.stderr)
        return 1
    data = json.loads(path.read_text(encoding="utf-8"))
    controls = data.get("controls") or {}
    missing = [k for k in REQUIRED if k not in controls]
    if missing:
        print("ERROR: missing controls:", ", ".join(missing), file=sys.stderr)
        return 1
    if "kb_card" not in (data.get("decor") or {}):
        print("ERROR: missing decor.kb_card", file=sys.stderr)
        return 1
    w, h = data["screen"]["width"], data["screen"]["height"]
    if w != 800 or h != 480:
        print(f"WARNING: expected 800x480, got {w}x{h}")
    print("OK: CLEAN_TABLO layout valid (main + keypad + settings)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
