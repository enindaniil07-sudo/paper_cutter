# -*- coding: utf-8 -*-
"""
Progress bar frames for Variable Icon (IconShow).
Default: 101 steps (0..100) = 0%..100% in 1% steps. Icons 70..170 → **26.icl**.

Run: python scripts/gen_paper_cutter_progress.py --project PAPER_CUTTER
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:
    raise SystemExit("pip install Pillow") from e


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "PAPER_CUTTER")
    ap.add_argument("--icon-base", type=int, default=70)
    ap.add_argument("--steps", type=int, default=101, help="Number of frames (101 = 1%% steps to 100%%)")
    args = ap.parse_args()
    root = args.project.resolve()
    layout = root / "design" / "layout.json"
    data = json.loads(layout.read_text(encoding="utf-8"))
    bar = data["controls"]["progress_bar"]
    w, h = int(bar["w"]), int(bar["h"])
    out = root / "image" / "progress"
    out.mkdir(parents=True, exist_ok=True)
    # remove old frames so packer does not keep 201 leftovers
    for old in out.glob("*.png"):
        old.unlink()
    base = int(args.icon_base)
    steps = max(2, int(args.steps))
    last = steps - 1

    for i in range(steps):
        im = Image.new("RGB", (w, h), (22, 26, 34))
        dr = ImageDraw.Draw(im)
        dr.rounded_rectangle((0, 0, w - 1, h - 1), radius=8, outline=(48, 56, 72), width=1)
        # Кадры 0..100%: заливка от края до края на 100%.
        if i <= 0:
            fill_w = 0
        elif i >= last:
            fill_w = w - 4
        else:
            fill_w = max(2, int((w - 4) * i / last))
        if fill_w > 0:
            x1 = 2
            x2 = min(w - 2, 2 + fill_w)
            for xx in range(x1, x2):
                t = (xx - x1) / max(fill_w, 1)
                r = int(36 + 200 * t)
                g = int(140 + 70 * t)
                b = int(210 - 30 * t)
                dr.line([(xx, 4), (xx, h - 5)], fill=(r, g, b))
        im.save(out / f"{base + i}.png")
    print(f"Wrote progress icons {base}..{base + steps - 1} ({steps} frames, bar only) ->", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
