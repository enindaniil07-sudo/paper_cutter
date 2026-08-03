# -*- coding: utf-8 -*-
"""
CLEAN_TABLO 14ShowFile.bin — ArtText + progress IconShow.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ENTRY0 = 0x10
MAIN_PTR = 0x4000
# 5 widgets × 32 = 0xA0
KB_PTR = 0x40A0
EMPTY_PTR = 0x40C0

ICON_H_LARGE = 56
ICON_H_SMALL = 36

ALIGN_LEFT = 0
ALIGN_RIGHT = 1

ART_VAR_LONG32 = 1
ART_VAR_UINT16 = 5

VP_PROGRESS = 0x6030
PROGRESS_ICON_MIN = 70
PROGRESS_ICON_MAX = 170  # 101 frames 0..100%


def put_entry(s: bytearray, page: int, count: int, ptr: int) -> None:
    o = ENTRY0 + page * 4
    s[o] = count & 0xFF
    s[o + 1] = (count >> 8) & 0xFF
    s[o + 2] = (ptr >> 8) & 0xFF
    s[o + 3] = ptr & 0xFF


def pack_arttext(
    *,
    vp: int,
    x: int,
    y: int,
    icon0: int,
    icon_lib: int,
    n_int: int,
    n_dot: int,
    sp: int,
    align: int,
    show0: bool = True,
    var_type: int = ART_VAR_UINT16,
) -> bytes:
    rec = bytearray(32)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0x5A03,
        sp & 0xFFFF,
        0x0009,
        vp & 0xFFFF,
        x & 0xFFFF,
        y & 0xFFFF,
        icon0 & 0xFFFF,
        ((icon_lib & 0xFF) << 8) | 0,
    )
    rec[16] = n_int & 0xFF
    rec[17] = n_dot & 0xFF
    rec[18] = var_type & 0xFF
    rec[19] = (align & 0x7F) | (0x80 if show0 else 0)
    rec[20] = 0
    rec[21] = 255
    rec[22] = 255
    rec[23] = 0
    return bytes(rec)


def pack_icon_progress(*, vp: int, x: int, y: int, sp: int) -> bytes:
    """Deprecated alias — prefer Process Bar; kept for older callers."""
    rec = bytearray(32)
    # Fall back still packs IconShow; CLEAN_TABLO uses form_clean_tablo_show_pages Process Bar.
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0x5A00,
        sp & 0xFFFF,
        0x000A,
        vp & 0xFFFF,
        x & 0xFFFF,
        y & 0xFFFF,
        0,
        100,
    )
    struct.pack_into(">HH", rec, 16, PROGRESS_ICON_MIN, PROGRESS_ICON_MAX)
    rec[20] = 26
    rec[21] = 1  # opaque — transparency caused garbage/disappear on some %
    rec[22] = 0
    rec[23] = 255
    rec[24] = 255
    rec[25] = 0
    return bytes(rec)


def _y_center(top: int, h: int, icon_h: int) -> int:
    return top + max(2, (h - icon_h) // 2)


def pack_main(layout: dict) -> bytes:
    c = layout["controls"]
    specs = (
        ("target_display", 0x6000, 30, 24, 5, 0, ICON_H_LARGE, ART_VAR_LONG32),
        ("travel_display", 0x6010, 30, 24, 5, 0, ICON_H_LARGE, ART_VAR_LONG32),
        ("speed_ms_display", 0x6020, 50, 25, 3, 2, ICON_H_SMALL, ART_VAR_UINT16),
        ("speed_rpm_display", 0x6024, 50, 25, 4, 0, ICON_H_SMALL, ART_VAR_UINT16),
    )
    out = bytearray()
    sp = 0x5100
    for key, vp, icon0, lib, n_int, n_dot, ih, vtype in specs:
        d = c[key]
        x0, y0, w, h = int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"])
        x = max(0, x0 + w - 8)
        y = _y_center(y0, h, ih)
        if key in ("speed_ms_display", "speed_rpm_display"):
            y += 4
        out += pack_arttext(
            vp=vp, x=x, y=y, icon0=icon0, icon_lib=lib,
            n_int=n_int, n_dot=n_dot, sp=sp,
            align=ALIGN_RIGHT, show0=True, var_type=vtype,
        )
        sp += 0x10

    pb = c["progress_bar"]
    out += pack_icon_progress(
        vp=VP_PROGRESS,
        x=int(pb["x"]),
        y=int(pb["y"]),
        sp=0x5140,
    )
    return bytes(out)


def pack_kb(layout: dict) -> bytes:
    d = layout["controls"]["kb_display"]
    x0, y0, w, h = int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"])
    y = y0 + max(8, (h - ICON_H_LARGE) // 2 - 8)
    return pack_arttext(
        vp=0x6080,
        x=x0 + w - 12,
        y=y,
        icon0=30,
        icon_lib=24,
        n_int=5,
        n_dot=0,
        sp=0x5180,
        align=ALIGN_RIGHT,
        show0=True,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    path = root / "DWIN_SET" / "14ShowFile.bin"
    if not path.is_file():
        raise SystemExit(f"missing {path}")
    s = bytearray(path.read_bytes())
    if len(s) < 0x50 or s[0] != 0x14:
        raise SystemExit(f"bad show file: {path}")

    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    need = EMPTY_PTR + 32
    if len(s) < need:
        s.extend(b"\x00" * (need - len(s)))

    main = pack_main(layout)
    n_main = len(main) // 32
    assert n_main == 5
    s[MAIN_PTR : MAIN_PTR + 5 * 32] = b"\xff" * (5 * 32)
    s[MAIN_PTR : MAIN_PTR + len(main)] = main
    s[KB_PTR : KB_PTR + 32] = pack_kb(layout)
    s[EMPTY_PTR : EMPTY_PTR + 32] = b"\xff" * 32

    if s[9] < 16:
        s[9] = 16
    maxp = s[9]

    put_entry(s, 0, n_main, MAIN_PTR)
    for p in range(1, 10):
        put_entry(s, p, 0, EMPTY_PTR)
    put_entry(s, 10, 0, EMPTY_PTR)
    for p in range(11, maxp + 1):
        put_entry(s, p, 0, EMPTY_PTR)

    path.write_bytes(s)
    print(
        f"OK ShowFile: page0@0x{MAIN_PTR:04X} x{n_main} "
        f"ArtText 6000/6010(remain whole m)/6020/6024 + IconShow 6030 progress"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
