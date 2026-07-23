# -*- coding: utf-8 -*-
"""CLEAN_TABLO page 16 — ввод расстояния торможения (UI only)."""
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

from clean_tablo_ui import AMBER
from paper_cutter_ui import font

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


def _sync(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".__new__")
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def render(w: int, h: int, layout: dict) -> Image.Image:
    c = layout["controls"]
    im = Image.new("RGB", (w, h), (16, 20, 28))
    dr = ImageDraw.Draw(im)

    # Title window
    lb = c["brake_label"]
    dr.rounded_rectangle(_box(lb), radius=14, fill=(32, 36, 48), outline=AMBER, width=2)
    title = "РАССТОЯНИЕ ТОРМОЖЕНИЯ"
    f_t = font(20, True)
    tw = dr.textlength(title, font=f_t) if hasattr(dr, "textlength") else 280
    dr.text(
        (lb["x"] + (lb["w"] - tw) / 2, lb["y"] + (lb["h"] - 22) / 2 - 1),
        title,
        fill=(255, 210, 154),
        font=f_t,
    )
    # unit hint on the right of label
    f_u = font(14, True)
    unit = "м"
    uw = dr.textlength(unit, font=f_u) if hasattr(dr, "textlength") else 12
    dr.text(
        (lb["x"] + lb["w"] - 28 - uw, lb["y"] + (lb["h"] - 16) / 2),
        unit,
        fill=(160, 170, 185),
        font=f_u,
    )

    # Digit window directly under title
    disp = c["brake_display"]
    dr.rounded_rectangle(_box(disp), radius=14, fill=(0, 0, 0), outline=AMBER, width=2)

    keys = [
        ("1", "brk_1", "num"),
        ("2", "brk_2", "num"),
        ("3", "brk_3", "num"),
        ("4", "brk_4", "num"),
        ("5", "brk_5", "num"),
        ("6", "brk_6", "num"),
        ("7", "brk_7", "num"),
        ("8", "brk_8", "num"),
        ("9", "brk_9", "num"),
        ("DEL", "brk_del", "del"),
        ("0", "brk_0", "num"),
        ("OK", "brk_ok", "ok"),
    ]
    f_k = font(30, True)
    f_del = font(22, True)
    for label, key, kind in keys:
        box = c[key]
        if kind == "ok":
            fill, outline, ink = (40, 140, 110), (120, 220, 180), (230, 255, 245)
        elif kind == "del":
            fill, outline, ink = (48, 54, 68), (150, 140, 110), (255, 230, 180)
        else:
            fill, outline, ink = (32, 42, 58), (90, 110, 140), (245, 250, 255)
        dr.rounded_rectangle(_box(box), radius=14, fill=fill, outline=outline, width=2)
        _center(dr, box, label, f_del if kind == "del" else f_k, ink)

    cc = c["brk_cancel"]
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
    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    w, h = int(layout["screen"]["width"]), int(layout["screen"]["height"])
    out = root / "image"
    out.mkdir(parents=True, exist_ok=True)
    path = out / "16.bmp"
    render(w, h, layout).save(path)
    _sync(path, root / "DWIN_SET" / "16.bmp")
    _sync(path, root / "16.bmp")
    (root / "source").mkdir(parents=True, exist_ok=True)
    render(w, h, layout).save(root / "source" / "page16_brake.png")
    print("Wrote", path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
