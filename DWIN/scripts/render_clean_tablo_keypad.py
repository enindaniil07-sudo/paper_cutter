# -*- coding: utf-8 -*-
"""CLEAN_TABLO keypad 10.bmp — solid full 800×480 page."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_scripts = Path(__file__).resolve().parent
if str(_scripts) not in sys.path:
    sys.path.insert(0, str(_scripts))
from clean_tablo_ui import AMBER  # noqa: E402
from paper_cutter_ui import font  # noqa: E402

try:
    from PIL import Image, ImageDraw
except ImportError as e:
    raise SystemExit("pip install Pillow") from e


def _box(c: dict) -> tuple[int, int, int, int]:
    return c["x"], c["y"], c["x"] + c["w"] - 1, c["y"] + c["h"] - 1


def _center(dr: ImageDraw.ImageDraw, c: dict, label: str, fnt, fill) -> None:
    tw = dr.textlength(label, font=fnt) if hasattr(dr, "textlength") else len(label) * 12
    th = getattr(fnt, "size", 22)
    x = c["x"] + (c["w"] - tw) / 2
    y = c["y"] + (c["h"] - th) / 2 - 1
    dr.text((x, y), label, fill=fill, font=fnt)


def render_keypad(w: int, h: int, layout: dict) -> Image.Image:
    im = Image.new("RGB", (w, h), (16, 20, 28))
    dr = ImageDraw.Draw(im)
    dr.text((40, 2), "ВВОД ЗАДАНО", fill=(255, 210, 154), font=font(20, True))

    disp = layout["controls"]["kb_display"]
    dr.rounded_rectangle(_box(disp), radius=14, fill=(0, 0, 0), outline=AMBER, width=2)

    keys = [
        ("1", "kb_1", "num"),
        ("2", "kb_2", "num"),
        ("3", "kb_3", "num"),
        ("4", "kb_4", "num"),
        ("5", "kb_5", "num"),
        ("6", "kb_6", "num"),
        ("7", "kb_7", "num"),
        ("8", "kb_8", "num"),
        ("9", "kb_9", "num"),
        ("DEL", "kb_del", "del"),
        ("0", "kb_0", "num"),
        ("OK", "kb_ok", "ok"),
    ]
    f_k = font(32, True)
    f_del = font(24, True)
    for label, key, kind in keys:
        c = layout["controls"][key]
        if kind == "ok":
            fill, outline, ink = (40, 140, 110), (120, 220, 180), (230, 255, 245)
        elif kind == "del":
            fill, outline, ink = (48, 54, 68), (150, 140, 110), (255, 230, 180)
        else:
            fill, outline, ink = (32, 42, 58), (90, 110, 140), (245, 250, 255)
        dr.rounded_rectangle(_box(c), radius=14, fill=fill, outline=outline, width=2)
        _center(dr, c, label, f_del if kind == "del" else f_k, ink)

    cc = layout["controls"]["kb_cancel"]
    dr.rounded_rectangle(_box(cc), radius=14, fill=(90, 42, 50), outline=(230, 120, 130), width=2)
    f_c = font(18, True)
    tw = dr.textlength("ОТМЕНА", font=f_c) if hasattr(dr, "textlength") else 70
    dr.text(
        (cc["x"] + (cc["w"] - tw) / 2, cc["y"] + cc["h"] / 2 - 10),
        "ОТМЕНА",
        fill=(255, 220, 225),
        font=f_c,
    )
    return im


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout_path = root / "design" / "layout.json"
    if not layout_path.is_file():
        print("ERROR: missing", layout_path, file=sys.stderr)
        return 1
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    w, h = int(layout["screen"]["width"]), int(layout["screen"]["height"])
    im = render_keypad(w, h, layout)
    for sub in ("image", "source"):
        (root / sub).mkdir(parents=True, exist_ok=True)
    out = root / "image" / "10.bmp"
    im.save(out, "BMP")
    preview = root / "source" / "page10_keypad.png"
    im.save(preview, "PNG")
    # Drop compact page-4 experiment
    for p in (root / "image" / "04.bmp", root / "04.bmp", root / "TFT" / "04.bmp",
              root / "source" / "page4_keypad_under_zado.png"):
        if p.is_file():
            p.unlink()
            print("Removed", p)
    print("Wrote", out, preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
