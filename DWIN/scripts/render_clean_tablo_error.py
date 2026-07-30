# -*- coding: utf-8 -*-
"""CLEAN_TABLO error pages 11..16 — reverse / no signal / no target / speed jump / channel / brake."""
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

from clean_tablo_ui import STOP, draw_soft_btn
from paper_cutter_ui import font

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as e:
    raise SystemExit("pip install Pillow") from e

# page_id → (title, body lines)
ERRORS: dict[int, tuple[str, tuple[str, ...]]] = {
    11: (
        "ОШИБКА ВРАЩЕНИЯ",
        (
            "Обнаружено вращение энкодера",
            "в обратную сторону.",
            "Движение назад запрещено.",
        ),
    ),
    12: (
        "НЕТ СИГНАЛА ЭНКОДЕРА",
        (
            "В режиме СТАРТ нет импульсов",
            "с энкодера дольше допустимого.",
            "Проверьте датчик и проводку.",
        ),
    ),
    13: (
        "НЕТ ЗАДАНИЯ",
        (
            "Нажат СТАРТ при ЗАДАНО = 0.",
            "Введите длину (тап по ЗАДАНО)",
            "и повторите запуск.",
        ),
    ),
    14: (
        "СКАЧОК СКОРОСТИ",
        (
            "Скорость резко выросла",
            "относительно текущего уровня.",
            "Проверьте колесо и крепление.",
        ),
    ),
    15: (
        "ОБРЫВ КАНАЛА A/B",
        (
            "Один канал энкодера активен,",
            "второй не меняется.",
            "Проверьте A (PA0) и B (PA1).",
        ),
    ),
    16: (
        "ТОРМОЗ НЕ ДЕЙСТВУЕТ",
        (
            "В зоне торможения скорость",
            "не снижается по энкодеру.",
            "Проверьте реле и механизм.",
        ),
    ),
}


def _sync(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".__new__")
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def render(w: int, h: int, title: str, lines: tuple[str, ...]) -> Image.Image:
    im = Image.new("RGB", (w, h), (18, 14, 16))
    px = im.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        for x in range(w):
            u = x / max(w - 1, 1)
            r = int(22 + 18 * (1 - t) + 10 * u)
            g = int(14 + 8 * (1 - t))
            b = int(16 + 6 * (1 - t))
            px[x, y] = (min(255, r), min(255, g), min(255, b))

    mist = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    md = ImageDraw.Draw(mist)
    md.ellipse((-80, -60, 420, 280), fill=(200, 40, 50, 40))
    md.ellipse((380, 200, 900, 560), fill=(120, 20, 30, 28))
    mist = mist.filter(ImageFilter.GaussianBlur(36))
    im = Image.alpha_composite(im.convert("RGBA"), mist).convert("RGB")
    dr = ImageDraw.Draw(im)

    box = (48, 90, w - 48, h - 120)
    dr.rounded_rectangle(box, radius=24, fill=(32, 22, 26), outline=(232, 96, 108), width=3)

    f_title = font(36, bold=True)
    tw = dr.textlength(title, font=f_title) if hasattr(dr, "textlength") else len(title) * 20
    dr.text(((w - tw) / 2, 140), title, fill=(255, 220, 220), font=f_title)

    f_body = font(24)
    y = 210
    for line in lines:
        lw = dr.textlength(line, font=f_body) if hasattr(dr, "textlength") else len(line) * 12
        dr.text(((w - lw) / 2, y), line, fill=(220, 200, 200), font=f_body)
        y += 36
    # No hint line under body — only the OK button.

    btn = draw_soft_btn(280, 72, "OK", STOP, pressed=False)
    bx, by = (w - 280) // 2, 380
    if btn.mode == "RGBA":
        im.paste(btn, (bx, by), btn)
    else:
        im.paste(btn, (bx, by))
    return im.convert("RGB") if im.mode != "RGB" else im


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    w, h = int(layout["screen"]["width"]), int(layout["screen"]["height"])
    out = root / "image"
    out.mkdir(parents=True, exist_ok=True)
    for page, (title, lines) in ERRORS.items():
        path = out / f"{page:02d}.bmp"
        render(w, h, title, lines).save(path)
        _sync(path, root / "DWIN_SET" / f"{page:02d}.bmp")
        _sync(path, root / f"{page:02d}.bmp")
        print("Wrote", path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
