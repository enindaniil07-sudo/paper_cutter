# -*- coding: utf-8 -*-
"""
CLEAN_TABLO 13TouchFile.bin

Page 0: RESET/STOP + gear→16 + Variable Data Input VP 6000 (keyboard page 10)
Page 10: ASCII keys for VarInput (ЗАДАНО)
Page 16: settings list — 4× VarInput (keyboard page 17) + НАЗАД→0
Page 17: ASCII keys for settings VarInput
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

PAGE_KB = 10
PAGE_SET = 16
PAGE_EDIT = 17


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
    rec[20] = 3  # Adj_Mode inching
    return bytes(rec)


def pack_var_input(
    page: int,
    touch: dict,
    disp: dict,
    vp: int,
    *,
    v_type: int,
    n_int: int,
    v_min: int,
    v_max: int,
    kb_page: int,
) -> bytes:
    """Variable Data Input — shows value + opens ASCII keyboard on kb_page."""
    xs, ys = int(touch["x"]), int(touch["y"])
    xe, ye = xs + int(touch["w"]), ys + int(touch["h"])
    # Digits grow left from cursor — place near right of value box.
    cx = int(disp["x"]) + int(disp["w"]) - 36
    cy = int(disp["y"]) + max(8, (int(disp["h"]) - 28) // 2)

    rec = bytearray(64)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        page,
        xs,
        ys,
        xe,
        ye,
        0xFF00,
        0xFF00,
        0xFE00,
    )
    rec[16] = 0xFE
    struct.pack_into(">H", rec, 17, vp & 0xFFFF)
    rec[19] = v_type & 0xFF  # 0=int16, 1=long
    rec[20] = n_int & 0xFF
    rec[21] = 0  # N_Dot
    struct.pack_into(">HH", rec, 22, cx & 0xFFFF, cy & 0xFFFF)
    struct.pack_into(">H", rec, 26, 0xFFFF)  # color
    rec[28] = 0  # Lib ASCII
    rec[29] = 24  # font
    rec[30] = 0xF8  # cursor color
    rec[31] = 1  # show digits
    rec[32] = 0xFE
    rec[33] = 1  # KB_Source = other page
    struct.pack_into(">H", rec, 34, kb_page & 0xFFFF)
    struct.pack_into(">HHHH", rec, 36, 0, 0, 799, 479)
    struct.pack_into(">HH", rec, 44, 0, 0)
    rec[48] = 0xFE
    rec[49] = 0xFF  # limits on
    struct.pack_into(">II", rec, 50, v_min & 0xFFFFFFFF, v_max & 0xFFFFFFFF)
    rec[58] = 0
    struct.pack_into(">H", rec, 59, 0)
    struct.pack_into(">H", rec, 61, 0)
    rec[63] = 0
    return bytes(rec)


def pack_var_input_zado(touch: dict, disp: dict) -> bytes:
    return pack_var_input(
        0,
        touch,
        disp,
        0x6000,
        v_type=1,
        n_int=5,
        v_min=0,
        v_max=99999,
        kb_page=PAGE_KB,
    )


def pack_ascii_key(page: int, c: dict, code: int) -> bytes:
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

SET_EDIT_KEYS = (
    ("set_edit_1", 0x0031),
    ("set_edit_2", 0x0032),
    ("set_edit_3", 0x0033),
    ("set_edit_4", 0x0034),
    ("set_edit_5", 0x0035),
    ("set_edit_6", 0x0036),
    ("set_edit_7", 0x0037),
    ("set_edit_8", 0x0038),
    ("set_edit_9", 0x0039),
    ("set_edit_del", 0x00F2),
    ("set_edit_0", 0x0030),
    ("set_edit_cancel", 0x00F0),
    ("set_edit_ok", 0x00F1),
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    layout = json.loads((root / "design" / "layout.json").read_text(encoding="utf-8"))
    c = layout["controls"]

    out = bytearray()
    out += pack_bit_button(0, c["btn_stop"], 0x6051, 2, -1)
    out += pack_bit_button(0, c["btn_reset"], 0x6052, 1, -1)
    settings = pack_bit_button(0, c["btn_settings"], 0x6055, -1, PAGE_SET)
    _pn = struct.unpack_from(">H", settings, 10)[0]
    if _pn != PAGE_SET:
        raise SystemExit(f"settings Pic_Next must be {PAGE_SET}, got {_pn:#x}")
    out += settings
    out += pack_var_input_zado(c["target_touch"], c["kb_display"])

    err_btn = {"x": 260, "y": 380, "w": 280, "h": 72}
    for page in (11, 12, 13, 14, 15):
        out += pack_bit_button(page, err_btn, 0x6054, -1, -1)

    for key, code in KB_KEYS:
        out += pack_ascii_key(PAGE_KB, c[key], code)

    # Page 16: back + four VarInputs (values shown by panel ASCII)
    out += pack_bit_button(PAGE_SET, c["btn_settings_back"], 0x6056, -1, 0)
    out += pack_var_input(
        PAGE_SET,
        c["set_row_brake"],
        c["set_val_brake"],
        0x6090,
        v_type=1,
        n_int=5,
        v_min=0,
        v_max=99999,
        kb_page=PAGE_EDIT,
    )
    # 6090 is LONG (4 bytes) → next free VP is 6094
    out += pack_var_input(
        PAGE_SET,
        c["set_row_on"],
        c["set_val_on"],
        0x6094,
        v_type=0,
        n_int=4,
        v_min=0,
        v_max=9999,
        kb_page=PAGE_EDIT,
    )
    out += pack_var_input(
        PAGE_SET,
        c["set_row_off"],
        c["set_val_off"],
        0x6096,
        v_type=0,
        n_int=4,
        v_min=0,
        v_max=9999,
        kb_page=PAGE_EDIT,
    )
    out += pack_var_input(
        PAGE_SET,
        c["set_val_spd"],
        c["set_val_spd"],
        0x6098,
        v_type=0,
        n_int=4,
        v_min=0,
        v_max=9999,
        kb_page=PAGE_EDIT,
    )
    # Speed-limit enable: OFF / ON (inching → MCU)
    out += pack_bit_button(PAGE_SET, c["set_spd_off"], 0x609A, -1, -1)
    out += pack_bit_button(PAGE_SET, c["set_spd_on"], 0x609B, -1, -1)

    for key, code in SET_EDIT_KEYS:
        out += pack_ascii_key(PAGE_EDIT, c[key], code)

    out += b"\xff\xff"

    path = root / "DWIN_SET" / "13TouchFile.bin"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
    print(f"Wrote {path} ({len(out)} bytes) settings VarInput page16->17")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
