# -*- coding: utf-8 -*-
"""CLEAN_TABLO soft-glass skins (modern HMI buttons)."""
from __future__ import annotations

from paper_cutter_ui import font

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as e:
    raise SystemExit("pip install Pillow") from e

# Soft modern accents (no purple / neon industrial)
AMBER = (245, 176, 92)
AQUA = (72, 196, 182)
SLATE = (154, 168, 186)
MUTE = (132, 146, 164)
START = (52, 196, 138)
RESET = (120, 148, 176)
STOP = (232, 96, 108)


def draw_soft_btn(
    w: int,
    h: int,
    label: str,
    accent: tuple[int, int, int],
    *,
    pressed: bool = False,
) -> Image.Image:
    """Large-radius soft pill button with subtle depth."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    if not pressed:
        sd.rounded_rectangle((4, 8, w - 2, h - 1), radius=22, fill=(0, 0, 0, 55))
        shadow = shadow.filter(ImageFilter.GaussianBlur(4))
    im = Image.alpha_composite(im, shadow)

    dr = ImageDraw.Draw(im)
    if pressed:
        fill = tuple(max(0, int(c * 0.55)) for c in accent) + (245,)
        outline = tuple(min(255, c + 20) for c in accent) + (160,)
        text = (248, 252, 255)
        ty_off = 2
    else:
        # soft tinted body
        fill = (
            max(0, min(255, int(accent[0] * 0.28 + 28))),
            max(0, min(255, int(accent[1] * 0.28 + 34))),
            max(0, min(255, int(accent[2] * 0.28 + 42))),
            235,
        )
        outline = (*accent, 140)
        text = tuple(min(255, c + 55) for c in accent)
        ty_off = 0

    # outer glass ring
    dr.rounded_rectangle((1, 1, w - 3, h - 4 if not pressed else h - 2), radius=22, fill=(255, 255, 255, 14), outline=(255, 255, 255, 28), width=1)
    # body
    pad = 5
    dr.rounded_rectangle(
        (pad, pad, w - pad - 1, h - pad - (3 if not pressed else 1)),
        radius=18,
        fill=fill,
        outline=outline,
        width=2,
    )
    # soft top highlight
    if not pressed:
        hi = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        hd = ImageDraw.Draw(hi)
        hd.rounded_rectangle((pad + 2, pad + 2, w - pad - 3, pad + h // 3), radius=14, fill=(255, 255, 255, 28))
        hi = hi.filter(ImageFilter.GaussianBlur(3))
        im = Image.alpha_composite(im, hi)
        dr = ImageDraw.Draw(im)

    f = font(min(34, h - 44), bold=True)
    tw = dr.textlength(label, font=f) if hasattr(dr, "textlength") else len(label) * 16
    tx = int((w - tw) / 2)
    ty = int((h - getattr(f, "size", 28)) / 2) + ty_off - 1
    dr.text((tx, ty), label, fill=text, font=f)
    return im.convert("RGB")
