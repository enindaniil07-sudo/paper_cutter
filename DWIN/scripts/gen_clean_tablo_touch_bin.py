# -*- coding: utf-8 -*-
"""
CLEAN_TABLO 13TouchFile.bin

Page 0: RESET/STOP + gear→16 + Variable Data Input VP 6000 (keyboard page 10)
Page 10: ASCII keys for VarInput
Page 16: braking-distance BitButtons (MCU VP 60B1–60BD)
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

PAGE_KB = 10
PAGE_SET = 16


def pack_bit_button(page: int, c: dict, vp: int, pic_on: int, pic_next: int) -> bytes:
    xs, ys = int(c["x"]), int(c["y"])
    xe, ye = xs + int(c["w"]), ys + int(c["h"])
    pic_on_u = 0xFF00 if pic_on < 0 else pic_on & 0xFFFF
    pic_next_u = 0xFF00 if pic_next < 0 else pic_next & 0xFFFF
    rec = bytearray(32)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        page,
        xs,
        ys,
        xe,
        ye,
        pic_next_u,
        pic_on_u,
        0xFE0D,
    )
    rec[16] = 0xFE
    struct.pack_into(">H", rec, 17, vp & 0xFFFF)
    rec[19] = 0  # Bit_Pos
    # Adj_Mode 3 = inching: press→1 + UART upload, release→0
    rec[20] = 3
    return bytes(rec)


def pack_var_input_zado(touch: dict, disp: dict) -> bytes:
    """Variable Data Input → VP 6000, long (4-byte), 5 digits, max 99999.

    Cursor is placed inside the keypad page digit window (kb_display), not the
    main-page ЗАДАНО card — otherwise tall ASCII digits clip the box on page 10.
    """
    xs, ys = int(touch["x"]), int(touch["y"])
    xe, ye = xs + int(touch["w"]), ys + int(touch["h"])
    # DWIN ASCII VarInput with N_Int draws right-aligned toward (cx,cy):
    # digits grow LEFT from the cursor. Put cx near the RIGHT of kb_display
    # so the number stays inside the amber rim (not past the left edge).
    cx = int(disp["x"]) + int(disp["w"]) - 28
    cy = int(disp["y"]) + 18

    rec = bytearray(64)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0,
        xs,
        ys,
        xe,
        ye,
        0xFF00,
        0xFF00,
        0xFE00,
    )
    rec[16] = 0xFE
    struct.pack_into(">H", rec, 17, 0x6000)  # VP ЗАДАНО
    # V_Type 0 = signed int16 (−32768…32767). V_Type 1 = long → до 99999.
    rec[19] = 0x01
    rec[20] = 5  # N_Int — пять цифр (макс. 99999)
    rec[21] = 0  # N_Dot
    struct.pack_into(">HH", rec, 22, cx & 0xFFFF, cy & 0xFFFF)
    struct.pack_into(">H", rec, 26, 0xFFFF)  # color
    rec[28] = 0  # Lib ASCII
    rec[29] = 24  # font
    rec[30] = 0xF8  # cursor color
    rec[31] = 1  # show digits (not stars)
    rec[32] = 0xFE
    rec[33] = 1  # KB_Source = other page
    struct.pack_into(">H", rec, 34, PAGE_KB)  # Pic_KB = 10
    # Keyboard covers page 10
    struct.pack_into(">HHHH", rec, 36, 0, 0, 799, 479)
    struct.pack_into(">HH", rec, 44, 0, 0)  # KB position on current page
    rec[48] = 0xFE
    rec[49] = 0xFF  # limits on
    struct.pack_into(">II", rec, 50, 0, 99999)  # V_min…V_max (макс. 5 цифр)
    rec[58] = 0  # Return_Set off
    struct.pack_into(">H", rec, 59, 0)  # Return_VP
    struct.pack_into(">H", rec, 61, 0)  # Return_DATA
    rec[63] = 0  # Layer_Gama opaque
    return bytes(rec)


def pack_ascii_key(page: int, c: dict, code: int) -> bytes:
    """16-byte Return Key Code for VarInput keyboard."""
    xs, ys = int(c["x"]), int(c["y"])
    xe, ye = xs + int(c["w"]), ys + int(c["h"])
    rec = bytearray(16)
    struct.pack_into(
        ">HHHHHHH",
        rec,
        0,
        page,
        xs,
        ys,
        xe,
        ye,
        0xFF00,
        0xFF00,
    )
    struct.pack_into(">H", rec, 14, code & 0xFFFF)
    return bytes(rec)


# DGUS keypad codes for Variable Data Input (page 10)
KB_KEYS = (
    ("kb_1", 0x0031),
    ("kb_2", 0x0032),
    ("kb_3", 0x0033),
    ("kb_4", 0x0034),
    ("kb_5", 0x0035),
    ("kb_6", 0x0036),
    ("kb_7", 0x0037),
    ("kb_8", 0x0038),
    ("kb_9", 0x0039),
    ("kb_del", 0x00F2),
    ("kb_0", 0x0030),
    ("kb_cancel", 0x00F0),
    ("kb_ok", 0x00F1),
)

# Page 16 braking-distance keypad → MCU BitButtons (ArtText VP 6090)
BRK_KEYS = (
    ("brk_1", 0x60B1),
    ("brk_2", 0x60B2),
    ("brk_3", 0x60B3),
    ("brk_4", 0x60B4),
    ("brk_5", 0x60B5),
    ("brk_6", 0x60B6),
    ("brk_7", 0x60B7),
    ("brk_8", 0x60B8),
    ("brk_9", 0x60B9),
    ("brk_del", 0x60BB),
    ("brk_0", 0x60BA),
    ("brk_cancel", 0x60BD),
    ("brk_ok", 0x60BC),
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    c = layout["controls"]

    out = bytearray()
    # Page 0: STOP / RESET / gear→16 / ЗАДАНО VarInput→page 10
    out += pack_bit_button(0, c["btn_stop"], 0x6051, 2, -1)
    out += pack_bit_button(0, c["btn_reset"], 0x6052, 1, -1)
    settings = pack_bit_button(0, c["btn_settings"], 0x6055, -1, PAGE_SET)
    import struct as _st

    _pn = _st.unpack_from(">H", settings, 10)[0]
    if _pn != PAGE_SET:
        raise SystemExit(f"settings Pic_Next must be {PAGE_SET}, got {_pn:#x}")
    out += settings
    out += pack_var_input_zado(c["target_touch"], c["kb_display"])

    err_btn = {"x": 260, "y": 380, "w": 280, "h": 72}
    for page in (11, 12, 13, 14, 15):
        out += pack_bit_button(page, err_btn, 0x6054, -1, -1)

    for key, code in KB_KEYS:
        out += pack_ascii_key(PAGE_KB, c[key], code)

    # Page 16: braking distance keypad (MCU ArtText VP 6090)
    for key, vp in BRK_KEYS:
        out += pack_bit_button(PAGE_SET, c[key], vp, -1, -1)

    out += b"\xff\xff"

    path = root / "DWIN_SET" / "13TouchFile.bin"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
    print(f"Wrote {path} ({len(out)} bytes) BitButton+VarInput, page16 brake")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
