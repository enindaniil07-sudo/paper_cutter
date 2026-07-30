# -*- coding: utf-8 -*-
"""CLEAN_TABLO page 16 (settings list) + page 17 (edit keypad)."""
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


def render_settings(w: int, h: int, layout: dict, *, invert_on: bool = False) -> Image.Image:
    c = layout["controls"]
    im = Image.new("RGB", (w, h), (16, 20, 28))
    dr = ImageDraw.Draw(im)

    title = c["settings_title"]
    dr.rounded_rectangle(_box(title), radius=14, fill=(32, 36, 48), outline=AMBER, width=2)
    f_t = font(22, True)
    t = "НАСТРОЙКИ"
    tw = dr.textlength(t, font=f_t) if hasattr(dr, "textlength") else 140
    dr.text(
        (title["x"] + (title["w"] - tw) / 2, title["y"] + (title["h"] - 24) / 2 - 1),
        t,
        fill=(255, 210, 154),
        font=f_t,
    )

    back = c["btn_settings_back"]
    dr.rounded_rectangle(_box(back), radius=14, fill=(90, 42, 50), outline=(230, 120, 130), width=2)
    f_b = font(18, True)
    _center(dr, back, "НАЗАД", f_b, (255, 220, 225))

    rows = [
        ("set_row_brake", "set_val_brake", "РАССТОЯНИЕ ТОРМОЖЕНИЯ", "м"),
        ("set_row_on", "set_val_on", "ВРЕМЯ ТОРМОЗ (1)", "мс"),
        ("set_row_off", "set_val_off", "ВРЕМЯ ОТПУСК (0)", "мс"),
    ]
    f_h = font(18, True)
    f_u = font(15, True)
    for row_k, val_k, head, unit in rows:
        row = c[row_k]
        val = c[val_k]
        dr.rounded_rectangle(_box(row), radius=14, fill=(28, 32, 42), outline=AMBER, width=2)
        dr.text((row["x"] + 16, row["y"] + (row["h"] - 20) / 2), head, fill=(255, 220, 180), font=f_h)
        dr.rounded_rectangle(_box(val), radius=10, fill=(0, 0, 0), outline=(120, 140, 160), width=2)
        dr.text(
            (val["x"] + val["w"] + 10, val["y"] + (val["h"] - 16) / 2),
            unit,
            fill=(160, 170, 185),
            font=f_u,
        )

    # Direction row + ИНВЕРСИЯ (off / on for Pic_On overlay page 19)
    row = c["set_row_dir"]
    inv = c["btn_enc_invert"]
    dr.rounded_rectangle(_box(row), radius=14, fill=(28, 32, 42), outline=AMBER, width=2)
    dr.text((row["x"] + 16, row["y"] + 18), "НАПРАВЛЕНИЕ", fill=(255, 220, 180), font=f_h)
    dr.text((row["x"] + 16, row["y"] + 46), "ВРАЩЕНИЯ", fill=(255, 220, 180), font=f_h)
    if invert_on:
        fill, outline, ink = (61, 107, 79), (126, 201, 154), (232, 255, 240)
    else:
        fill, outline, ink = (20, 24, 32), (90, 101, 120), (160, 170, 185)
    dr.rounded_rectangle(_box(inv), radius=12, fill=fill, outline=outline, width=2)
    f_inv = font(18, True)
    _center(dr, inv, "ИНВЕРСИЯ", f_inv, ink)
    return im


def render_edit(w: int, h: int, layout: dict) -> Image.Image:
    c = layout["controls"]
    im = Image.new("RGB", (w, h), (16, 20, 28))
    dr = ImageDraw.Draw(im)

    disp = c["set_edit_display"]
    dr.rounded_rectangle(_box(disp), radius=14, fill=(0, 0, 0), outline=AMBER, width=2)
    f_hint = font(16, True)
    hint = "ВВОД ЗНАЧЕНИЯ"
    hw = dr.textlength(hint, font=f_hint) if hasattr(dr, "textlength") else 160
    dr.text(
        (disp["x"] + 20, disp["y"] + 12),
        hint,
        fill=(160, 170, 185),
        font=f_hint,
    )
    # keep right side free for VarInput digits
    _ = hw

    keys = [
        ("1", "set_edit_1", "num"),
        ("2", "set_edit_2", "num"),
        ("3", "set_edit_3", "num"),
        ("4", "set_edit_4", "num"),
        ("5", "set_edit_5", "num"),
        ("6", "set_edit_6", "num"),
        ("7", "set_edit_7", "num"),
        ("8", "set_edit_8", "num"),
        ("9", "set_edit_9", "num"),
        ("DEL", "set_edit_del", "del"),
        ("0", "set_edit_0", "num"),
        ("OK", "set_edit_ok", "ok"),
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

    cc = c["set_edit_cancel"]
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
    (root / "source").mkdir(parents=True, exist_ok=True)

    # page16 = error «тормоз не действует» (render_clean_tablo_error.py).
    # Do not overwrite with a blank — that clobbers the error BMP.

    # page17 = settings list (ArtText digits live here)
    p17 = render_settings(w, h, layout, invert_on=False)
    path17 = out / "17.bmp"
    p17.save(path17)
    _sync(path17, root / "DWIN_SET" / "17.bmp")
    _sync(path17, root / "17.bmp")
    p17.save(root / "source" / "page17_settings.png")

    # page19 = green ON look (kept for reference / optional Pic_On)
    p19 = render_settings(w, h, layout, invert_on=True)
    path19 = out / "19.bmp"
    p19.save(path19)
    _sync(path19, root / "DWIN_SET" / "19.bmp")
    _sync(path19, root / "19.bmp")
    p19.save(root / "source" / "page19_invert_on.png")

    # Variable Icon tiles (27.icl): sticky ON/OFF for VP 6098
    inv = layout["controls"]["btn_enc_invert"]
    ix, iy = int(inv["x"]), int(inv["y"])
    iw, ih = int(inv["w"]), int(inv["h"])
    icon_dir = out / "invert_btn"
    icon_dir.mkdir(parents=True, exist_ok=True)
    for name, src in (("00.bmp", p17), ("01.bmp", p19)):
        tile = src.crop((ix, iy, ix + iw, iy + ih))
        tip = icon_dir / name
        tile.save(tip)
        _sync(tip, root / "DWIN_SET" / "invert_btn" / name)

    # page18 = settings edit keypad
    p18 = render_edit(w, h, layout)
    path18 = out / "18.bmp"
    p18.save(path18)
    _sync(path18, root / "DWIN_SET" / "18.bmp")
    _sync(path18, root / "18.bmp")
    p18.save(root / "source" / "page18_settings_edit.png")

    print(
        "Wrote",
        path17,
        "(settings),",
        path19,
        "(invert ON),",
        path18,
        "(edit),",
        icon_dir,
        "(icon tiles)",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
