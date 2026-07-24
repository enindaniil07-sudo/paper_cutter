# -*- coding: utf-8 -*-
"""CLEAN_TABLO digit icons — mono cells, opaque near-black BG (not chroma 0,0,0).

Pure black BG is chroma-keyed by DGUS → old digits ghost (наложение). PAPER_CUTTER
uses (12,14,18) so each cell overwrites the previous glyph cleanly.
"""
from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:
    raise SystemExit("pip install Pillow") from e

# ЗАДАНО / ОСТАЛОСЬ wells are 112px tall; remain well is 280px → 5×56 fills it.
TW_L, TH_L = 56, 112
# Settings wells: taller than old 28×36, narrower than large 56 — big look, tight pitch
TW_S, TH_S = 34, 52
# Opaque tile (not 0,0,0) so ArtText does not chroma-eat the cell
BG = (12, 14, 18)


def _font(px: int):
    for n in ("arialbd.ttf", "segoeuib.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(n, px)
        except OSError:
            continue
    return ImageFont.load_default()


def _glyph(ch: str, tw: int, th: int, font_px: int, color: tuple[int, int, int]) -> Image.Image:
    im = Image.new("RGB", (tw, th), BG)
    dr = ImageDraw.Draw(im)
    font = _font(font_px)
    bbox = dr.textbbox((0, 0), ch, font=font)
    wch, hch = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = (tw - wch) // 2 - bbox[0]
    ty = (th - hch) // 2 - bbox[1]
    dr.text((tx, ty), ch, fill=color, font=font)
    return im


def _emit(
    folder: Path,
    base: int,
    tw: int,
    th: int,
    color: tuple[int, int, int],
    *,
    fill: float = 0.78,
) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    # Allow font a bit wider than cell for bold look; pitch stays = tw
    font_px = max(16, min(int(th * fill), tw + 8))
    for d in range(10):
        _glyph(str(d), tw, th, font_px, color).save(folder / f"{base + d}.png")
    # decimal: SAME cell as digits → no shift/overlap in XX.XX
    _glyph(".", tw, th, font_px, color).save(folder / f"{base + 10}.png")
    print(f"Wrote mono {tw}x{th} icons {base}-{base + 10} BG={BG} font={font_px} -> {folder}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    # Fill the 112×(280/336) wells: max font in cell
    _emit(root / "image" / "digits_large", 30, TW_L, TH_L, (255, 220, 150), fill=0.92)
    # Amber on near-black wells; fill high so glyph looks big inside narrow cell
    _emit(root / "image" / "digits_small", 50, TW_S, TH_S, (255, 220, 150), fill=0.90)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
