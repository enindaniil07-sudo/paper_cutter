# -*- coding: utf-8 -*-
"""CLEAN_TABLO: 01/02.bmp full-screen Pic_On for СБРОС / СТОП."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_scripts = Path(__file__).resolve().parent
if str(_scripts) not in sys.path:
    sys.path.insert(0, str(_scripts))
from clean_tablo_ui import RESET, STOP, draw_soft_btn  # noqa: E402

try:
    from PIL import Image
except ImportError as e:
    raise SystemExit("pip install Pillow") from e

OVERLAYS = (
    ("01.bmp", "btn_reset", "СБРОС", RESET),
    ("02.bmp", "btn_stop", "СТОП", STOP),
)


def _rect(d: dict) -> tuple[int, int, int, int]:
    return int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout_path = root / "design" / "layout.json"
    idle = root / "image" / "00.bmp"
    if not layout_path.is_file() or not idle.is_file():
        print("ERROR: need layout.json and image/00.bmp", file=sys.stderr)
        return 1
    data = json.loads(layout_path.read_text(encoding="utf-8"))
    c = data["controls"]
    base = Image.open(idle).convert("RGB")
    for name, key, label, accent in OVERLAYS:
        if key not in c:
            continue
        r = _rect(c[key])
        im = base.copy()
        im.paste(draw_soft_btn(r[2], r[3], label, accent, pressed=True), (r[0], r[1]))
        out = root / "image" / name
        im.save(out, "BMP")
        print("Wrote", out)
    # Drop old START pressed skin if present
    for stale in (root / "image" / "03.bmp", root / "03.bmp", root / "DWIN_SET" / "03.bmp"):
        if stale.is_file():
            stale.unlink()
            print("Removed", stale)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
