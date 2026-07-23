# -*- coding: utf-8 -*-
"""
Pack this project's DWIN_SET screen/page BMPs into a single DGUS-style .icl (JPEG records).

Default for TEST_PROJECT_2: **00.bmp** (page) + **01.bmp** / **02.bmp** (pressed skins) → **DWIN_SET/23.icl** (flash slot **23**).
That matches a typical T5LCFG.CFG byte **0x08 = 0x17** (23) from DGUS stock templates.
Icon ids follow leading digits in filenames (00→0, 01→1, …).
For each listed **.bmp**, if **image/<name>** exists and is newer than **DWIN_SET/<name>**,
the packer uses **image/** (so locked files under DWIN_SET do not block ICL refresh after render).

Run from repo root:
  python scripts/pack_dwin_set_screen_to_icl.py --project TEST_PROJECT_2

Requires: pip install Pillow (same as images_to_icl.py).
"""
from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


def _load_images_to_icl():
    here = Path(__file__).resolve().parent
    path = here / "images_to_icl.py"
    name = "_images_to_icl_pack"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load images_to_icl.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser(description="Pack DWIN_SET/*.bmp into one .icl from current project assets.")
    ap.add_argument(
        "--project",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "TEST_PROJECT_2",
        help="Project folder containing DWIN_SET/",
    )
    ap.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output .icl path (default: <project>/DWIN_SET/23.icl)",
    )
    ap.add_argument("--icl-id", type=int, default=32, help="ICL flash slot id (default 32, matches T5LCFG 0x08=0x20)")
    ap.add_argument(
        "--bmps",
        type=str,
        default="00.bmp,01.bmp,02.bmp,03.bmp,10.bmp",
        help="Comma-separated filenames under image/ (default: real pages only, no fake 04-09)",
    )
    ap.add_argument("--quality", type=int, default=95, help="JPEG quality 1–95 (DGUS-like ~95)")
    ap.add_argument(
        "--chroma",
        type=str,
        default="444",
        choices=("444", "411"),
        help="JPEG chroma: 444 matches DGUS size/quality better than 411",
    )
    args = ap.parse_args()
    root: Path = args.project.resolve()
    dset = root / "DWIN_SET"
    if not dset.is_dir():
        print("ERROR: missing DWIN_SET:", dset, file=sys.stderr)
        return 1
    names = [x.strip() for x in args.bmps.split(",") if x.strip()]
    imgdir = root / "image"

    def pick(n: str) -> Path:
        p_d = dset / n
        p_i = imgdir / n
        if p_i.is_file():
            return p_i
        return p_d

    paths = [pick(n) for n in names]
    for p in paths:
        if not p.is_file():
            print("ERROR: missing image:", p, file=sys.stderr)
            return 1
    out = args.output if args.output else (dset / f"{args.icl_id}.icl")
    out = out.resolve()

    # Stale wrong-slot library from earlier builds
    for stale_id in (23, 32):
        stale = dset / f"{stale_id}.icl"
        if stale.is_file() and stale.resolve() != out:
            stale.unlink()
            print("Removed stale", stale.name)

    mod = _load_images_to_icl()
    try:
        _, slot_id, n, mx = mod.run_pack(
            out,
            paths,
            icl_id=args.icl_id,
            quality=args.quality,
            align_head_kb=32,
            lib_size_kb=256,
            chroma=args.chroma,
            keep_jpeg_raw=False,
        )
    except Exception as e:
        print("ERROR:", e, file=sys.stderr)
        return 1
    print(f"Wrote {out} (slot={slot_id}, images={n}, max_icon_id={mx}, q={args.quality}, chroma={args.chroma})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
