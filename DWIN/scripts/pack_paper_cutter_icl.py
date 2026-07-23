# -*- coding: utf-8 -*-
"""Pack digit/progress PNG folders into separate .icl files for PAPER_CUTTER."""
from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


def _load_images_to_icl():
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("_icl_pack_pc", here / "images_to_icl.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load images_to_icl.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules["_icl_pack_pc"] = mod
    spec.loader.exec_module(mod)
    return mod


def _pack_folder(out: Path, paths: list[Path], icl_id: int, quality: int) -> int:
    mod = _load_images_to_icl()
    try:
        _, slot_id, n, mx = mod.run_pack(
            out,
            paths,
            icl_id=icl_id,
            quality=quality,
            align_head_kb=32,
            lib_size_kb=256,
            chroma="411",
            keep_jpeg_raw=False,
        )
    except Exception as e:
        print("ERROR:", e, file=sys.stderr)
        return 1
    print(f"Wrote {out} (slot={slot_id}, images={n}, max_icon_id={mx})")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "PAPER_CUTTER")
    ap.add_argument("--which", choices=("large", "small", "progress", "all"), default="all")
    ap.add_argument("--quality", type=int, default=88)
    args = ap.parse_args()
    root = args.project.resolve()
    dset = root / "DWIN_SET"
    dset.mkdir(parents=True, exist_ok=True)

    specs: list[tuple[str, Path, list[int], int]] = []
    if args.which in ("large", "all"):
        specs.append(("large", root / "image" / "digits_large", list(range(30, 41)), 24))
    if args.which in ("small", "all"):
        specs.append(("small", root / "image" / "digits_small", list(range(50, 61)), 25))
    if args.which in ("progress", "all"):
        n_prog = 101  # icons 70..170 = 0..100 %
        specs.append(("progress", root / "image" / "progress", list(range(70, 70 + n_prog)), 26))

    for name, folder, ids, icl_id in specs:
        if not folder.is_dir():
            print("ERROR: run gen script first, missing", folder, file=sys.stderr)
            return 1
        paths = [folder / f"{i}.png" for i in ids]
        for p in paths:
            if not p.is_file():
                print("ERROR: missing", p, file=sys.stderr)
                return 1
        rc = _pack_folder(dset / f"{icl_id}.icl", paths, icl_id, args.quality)
        if rc != 0:
            return rc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
