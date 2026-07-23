# -*- coding: utf-8 -*-
"""
CLEAN_TABLO 00.bmp — soft glass: ЗАДАНО / ОСТАЛОСЬ + progress + speed/RPM + buttons.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import sys
from pathlib import Path

_scripts = Path(__file__).resolve().parent
if str(_scripts) not in sys.path:
    sys.path.insert(0, str(_scripts))
from clean_tablo_ui import (  # noqa: E402
    AMBER,
    AQUA,
    MUTE,
    RESET,
    SLATE,
    START,
    STOP,
    draw_soft_btn,
)
from paper_cutter_ui import font, rect_box  # noqa: E402

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as e:
    raise SystemExit("pip install Pillow") from e


def _rect(d: dict) -> tuple[int, int, int, int]:
    return int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"])


def _sync_bmp(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".__new__")
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def _bg(w: int, h: int) -> Image.Image:
    im = Image.new("RGB", (w, h), (18, 22, 30))
    px = im.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        for x in range(w):
            u = x / max(w - 1, 1)
            # soft charcoal → cool slate, ambient light from top-left
            lift = 18 * (1 - t) * (0.55 + 0.45 * math.cos(u * math.pi))
            r = int(16 + lift + 6 * u * t)
            g = int(20 + lift * 0.95 + 4 * (1 - u))
            b = int(28 + lift * 0.85 + 8 * (1 - t))
            px[x, y] = (min(255, r), min(255, g), min(255, b))

    mist = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    md = ImageDraw.Draw(mist)
    md.ellipse((-160, -120, 340, 260), fill=(*AMBER, 26))
    md.ellipse((420, -140, 980, 280), fill=(*AQUA, 24))
    md.ellipse((180, 300, 620, 560), fill=(90, 120, 160, 16))
    mist = mist.filter(ImageFilter.GaussianBlur(42))
    return Image.alpha_composite(im.convert("RGBA"), mist).convert("RGB")


def _glass_card(
    im: Image.Image,
    rim: tuple[int, int, int, int],
    title: str,
    unit: str,
    accent: tuple[int, int, int],
    *,
    well: bool = True,
    right_reserve: int = 0,
) -> None:
    overlay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    dr = ImageDraw.Draw(overlay)
    x, y, rw, rh = rim

    # soft drop shadow
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    sd.rounded_rectangle((x + 3, y + 6, x + rw + 1, y + rh + 3), radius=24, fill=(0, 0, 0, 48))
    sh = sh.filter(ImageFilter.GaussianBlur(5))
    overlay = Image.alpha_composite(overlay, sh)
    dr = ImageDraw.Draw(overlay)

    # glass plate
    dr.rounded_rectangle(
        rect_box(rim),
        radius=22,
        fill=(255, 255, 255, 16),
        outline=(255, 255, 255, 36),
        width=1,
    )
    # inner fill
    dr.rounded_rectangle(
        (x + 1, y + 1, x + rw - 2, y + rh - 2),
        radius=21,
        fill=(22, 28, 38, 168),
    )
    # accent dot (no hairline toward units — looked like a white stripe)
    dr.ellipse((x + 18, y + 20, x + 28, y + 30), fill=(*accent, 230))

    if well:
        # Near-black well: ArtText digit BG (0,0,0) chroma-keys cleanly — no ghost boxes
        dr.rounded_rectangle(
            (x + 16, y + 46, x + rw - 17, y + rh - 16),
            radius=14,
            fill=(0, 0, 0, 230),
            outline=(255, 255, 255, 16),
            width=1,
        )

    im_rgb = Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")
    im.paste(im_rgb)
    d = ImageDraw.Draw(im)
    f_cap = font(12, bold=True)
    f_unit = font(11)
    d.text((x + 36, y + 16), title, fill=tuple(min(255, c + 30) for c in accent), font=f_cap)
    if unit:
        tw = d.textlength(unit, font=f_unit) if hasattr(d, "textlength") else 20
        d.text(
            (x + rw - 18 - tw - max(0, right_reserve), y + 17),
            unit,
            fill=MUTE,
            font=f_unit,
        )


def _draw_gear(dr: ImageDraw.ImageDraw, cx: int, cy: int, r: int, fill) -> None:
    import math

    teeth = 8
    r_outer = r
    r_inner = int(r * 0.62)
    r_hub = int(r * 0.28)
    pts = []
    for i in range(teeth * 2):
        ang = (i * math.pi / teeth) - math.pi / 2
        rr = r_outer if (i % 2 == 0) else r_inner
        if i % 2 == 0:
            for da in (-0.12, 0.12):
                a = ang + da
                pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        else:
            pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    dr.polygon(pts, fill=fill)
    dr.ellipse((cx - r_hub, cy - r_hub, cx + r_hub, cy + r_hub), fill=(16, 20, 28))


def _settings_gear(im: Image.Image, c: dict) -> None:
    """Gear chip — top-right of main screen."""
    x, y, w, h = _rect(c)
    overlay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    dr = ImageDraw.Draw(overlay)
    dr.rounded_rectangle(
        (x, y, x + w - 1, y + h - 1),
        radius=14,
        fill=(36, 40, 52, 230),
        outline=(*AMBER, 200),
        width=2,
    )
    im_rgb = Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")
    im.paste(im_rgb)
    d = ImageDraw.Draw(im)
    _draw_gear(
        d,
        x + w // 2,
        y + h // 2,
        min(w, h) // 2 - 10,
        (255, 210, 154),
    )

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root: Path = args.project.resolve()
    layout_path = root / "design" / "layout.json"
    if not layout_path.is_file():
        print("ERROR: missing", layout_path, file=sys.stderr)
        return 1

    data = json.loads(layout_path.read_text(encoding="utf-8"))
    w, h = int(data["screen"]["width"]), int(data["screen"]["height"])
    c = data["controls"]
    decor = data.get("decor") or {}

    im = _bg(w, h)
    if "target_rim" in decor:
        _glass_card(im, _rect(decor["target_rim"]), "ЗАДАНО", "м", AMBER)
    if "travel_rim" in decor:
        _glass_card(im, _rect(decor["travel_rim"]), "ОСТАЛОСЬ", "м", AQUA)

    if "speed_ms_rim" in decor:
        _glass_card(im, _rect(decor["speed_ms_rim"]), "СКОРОСТЬ", "м/с", AQUA, well=True)
    elif "speed_ms_rim" in decor and "speed_rpm_rim" in decor:
        _glass_card(im, _rect(decor["speed_ms_rim"]), "СКОРОСТЬ", "м/с", AQUA, well=True)
        _glass_card(im, _rect(decor["speed_rpm_rim"]), "ОБОРОТЫ", "об/мин", SLATE, well=True)
    elif "speed_strip" in decor:
        sr = _rect(decor["speed_strip"])
        mid = sr[2] // 2 - 8
        _glass_card(im, (sr[0], sr[1], mid, sr[3]), "СКОРОСТЬ", "м/с", AQUA, well=True)
        _glass_card(im, (sr[0] + mid + 16, sr[1], mid, sr[3]), "ОБОРОТЫ", "об/мин", SLATE, well=True)

    # Трек под Process Bar 0x5A23 (VP 6030) — панель рисует заливку поверх.
    if "progress_bar" in c:
        px, py, pw, ph = _rect(c["progress_bar"])
        d = ImageDraw.Draw(im)
        d.rounded_rectangle(
            (px, py, px + pw - 1, py + ph - 1),
            radius=8,
            fill=(16, 20, 28),
            outline=(40, 48, 64),
            width=1,
        )

    for key, label, acc in (
        ("btn_start", "СТАРТ", START),
        ("btn_reset", "СБРОС", RESET),
        ("btn_stop", "СТОП", STOP),
    ):
        if key not in c:
            continue
        r = _rect(c[key])
        im.paste(draw_soft_btn(r[2], r[3], label, acc, pressed=False), (r[0], r[1]))

    if "btn_settings" in c:
        _settings_gear(im, c["btn_settings"])

    for sub in ("image", "source"):
        (root / sub).mkdir(parents=True, exist_ok=True)
    out_img = root / "image" / "00.bmp"
    im.save(out_img, "BMP")
    preview = root / "source" / "page0_800x480.png"
    im.save(preview, "PNG")
    print("Wrote", out_img, preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
