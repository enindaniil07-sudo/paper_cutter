# -*- coding: utf-8 -*-
"""Shared UI helpers for PAPER_CUTTER screen rendering."""
from __future__ import annotations

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:
    raise SystemExit("pip install Pillow") from e


def font(sz: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    names = ("arialbd.ttf", "segoeuib.ttf") if bold else ("segoeui.ttf", "arial.ttf")
    for n in names:
        try:
            return ImageFont.truetype(n, sz)
        except OSError:
            continue
    return ImageFont.load_default()


def rect_box(r: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    x, y, w, h = r
    return x, y, x + w - 1, y + h - 1


def draw_adj_btn(
    w: int,
    h: int,
    label: str,
    accent: tuple[int, int, int],
    *,
    pressed: bool = False,
) -> Image.Image:
    base = (28, 32, 40) if pressed else (34, 38, 48)
    inner = (18, 22, 30) if pressed else (24, 28, 36)
    im = Image.new("RGB", (w, h), base)
    dr = ImageDraw.Draw(im)
    outline = tuple(min(255, c + (35 if pressed else 50)) for c in accent)
    dr.rounded_rectangle((2, 2, w - 3, h - 3), radius=8, outline=outline, width=2)
    dr.rounded_rectangle((4, 4, w - 5, h - 5), radius=6, fill=inner)
    pad = 10
    max_tw = max(8, w - pad)
    fs = min(22, h - 18)
    f = font(fs, bold=True)
    tw = dr.textlength(label, font=f) if hasattr(dr, "textlength") else len(label) * 10
    while tw > max_tw and fs > 12:
        fs -= 1
        f = font(fs, bold=True)
        tw = dr.textlength(label, font=f) if hasattr(dr, "textlength") else len(label) * (fs // 2)
    tx = max(pad // 2, int((w - tw) / 2))
    ty = int((h - fs) / 2) - 1 + (2 if pressed else 0)
    fill = (245, 248, 252) if pressed else (235, 240, 248)
    dr.text((tx, ty), label, fill=fill, font=f)
    return im


def draw_action_btn(
    w: int,
    h: int,
    label: str,
    fill: tuple[int, int, int],
    accent: tuple[int, int, int],
    *,
    pressed: bool = False,
) -> Image.Image:
    base = tuple(max(0, c - 24) for c in fill) if pressed else fill
    im = Image.new("RGB", (w, h), base)
    dr = ImageDraw.Draw(im)
    outline = tuple(min(255, c + (30 if pressed else 50)) for c in accent)
    dr.rounded_rectangle((2, 2, w - 3, h - 3), radius=10, outline=outline, width=2)
    if pressed:
        dr.rounded_rectangle((5, 5, w - 6, h - 6), radius=8, fill=tuple(max(0, c - 12) for c in base))
    f = font(min(24, h - 20), bold=True)
    tw = dr.textlength(label, font=f) if hasattr(dr, "textlength") else len(label) * 12
    tx = int((w - tw) / 2)
    ty = int((h - (f.size if hasattr(f, "size") else 22)) / 2) + (2 if pressed else 0)
    dr.text((tx, ty), label, fill=(250, 252, 255), font=f)
    return im
