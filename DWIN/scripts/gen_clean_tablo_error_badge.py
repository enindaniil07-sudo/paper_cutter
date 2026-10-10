# -*- coding: utf-8 -*-
"""CLEAN_TABLO error badge — 28.icl frames 80..82 (скрыто / ЭНК. / ТОРМ.).

IconShow VP 6040 на стр. 0 рисует кадр ровно в окошке controls.error_badge.
Фон каждого кадра — вырезка из image/00.bmp под этим окошком, так что
кадр «скрыто» неотличим от фона, а бейдж не выходит за границы окошка.
Запускать после render_clean_tablo_screen.py.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from paper_cutter_ui import font

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as e:
    raise SystemExit("pip install Pillow") from e

ICON_HIDDEN = 80
ICON_ENCODER = 81
ICON_BRAKE = 82

# (icon id, подпись, акцент). Короткие подписи — читаются крупным шрифтом.
FRAMES: tuple[tuple[int, str | None, tuple[int, int, int]], ...] = (
    (ICON_HIDDEN, None, (0, 0, 0)),
    (ICON_ENCODER, "ЭНК.", (245, 176, 92)),
    (ICON_BRAKE, "ТОРМ.", (232, 96, 108)),
)


def _background(root: Path, x: int, y: int, w: int, h: int) -> Image.Image:
    src = root / "image" / "00.bmp"
    if not src.is_file():
        print("ERROR: missing", src, "— run render_clean_tablo_screen.py first", file=sys.stderr)
        raise SystemExit(1)
    return Image.open(src).convert("RGB").crop((x, y, x + w, y + h))


def _render(bg: Image.Image, label: str | None, accent: tuple[int, int, int]) -> Image.Image:
    w, h = bg.size
    if not label:
        return bg.copy()

    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))

    # Мягкое свечение за кружком.
    cx, cy = w // 2, 38
    r = 22
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((cx - r - 10, cy - r - 10, cx + r + 10, cy + r + 10), fill=(*accent, 80))
    glow = glow.filter(ImageFilter.GaussianBlur(7))
    overlay = Image.alpha_composite(overlay, glow)
    dr = ImageDraw.Draw(overlay)

    # Карточка строго внутри окошка (отступ 1 px от края).
    dr.rounded_rectangle(
        (1, 1, w - 2, h - 2),
        radius=16,
        fill=(30, 18, 20, 238),
        outline=(*accent, 220),
        width=2,
    )

    # Кружок с «!».
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(*accent, 240))
    f_bang = font(34, bold=True)
    bang = "!"
    bw = dr.textlength(bang, font=f_bang) if hasattr(dr, "textlength") else 12
    dr.text((cx - bw / 2, cy - 21), bang, fill=(22, 16, 18), font=f_bang)

    # Подпись: максимально крупно, но не шире окошка.
    size = 20
    f_lab = font(size, bold=True)
    lw = dr.textlength(label, font=f_lab) if hasattr(dr, "textlength") else len(label) * size * 0.6
    while lw > w - 12 and size > 11:
        size -= 1
        f_lab = font(size, bold=True)
        lw = dr.textlength(label, font=f_lab) if hasattr(dr, "textlength") else len(label) * size * 0.6
    dr.text(
        ((w - lw) / 2, h - 14 - size),
        label,
        fill=tuple(min(255, c + 40) for c in accent),
        font=f_lab,
    )

    return Image.alpha_composite(bg.convert("RGBA"), overlay).convert("RGB")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    box = layout["controls"]["error_badge"]
    x, y, w, h = int(box["x"]), int(box["y"]), int(box["w"]), int(box["h"])
    bg = _background(root, x, y, w, h)

    out = root / "image" / "error_badge"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    for icon_id, label, accent in FRAMES:
        path = out / f"{icon_id}.png"
        _render(bg, label, accent).save(path)
        print("Wrote", path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
