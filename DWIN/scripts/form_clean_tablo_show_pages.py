# -*- coding: utf-8 -*-
"""
CLEAN_TABLO show patch — DGUS Save->Generate container is sacred.

1) Insert IconShow VP6030 into page0 (shift later widgets, fix pointers)
2) LONG32/UINT16 + white digits / Icon0=30 on ArtText
3) If DGUS already put ArtText on page17 — KEEP it (do not clear)
4) page16/18 stay EMPTY at FF-sentinel (never point empty→MAIN 0x4000)

Never append new ArtText on 16/17/18 from scratch in Python.
Never rewrite the whole page directory from scratch.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

ENTRY0 = 0x10
PAYLOAD = 0x4000

ART_VAR_LONG32 = 1
ART_VAR_UINT16 = 5


def put_entry(s: bytearray, page: int, count: int, ptr: int) -> None:
    o = ENTRY0 + page * 4
    s[o] = count & 0xFF
    s[o + 1] = (count >> 8) & 0xFF
    s[o + 2] = (ptr >> 8) & 0xFF
    s[o + 3] = ptr & 0xFF


def get_entry(s: bytes, page: int) -> tuple[int, int]:
    o = ENTRY0 + page * 4
    return s[o] | (s[o + 1] << 8), (s[o + 2] << 8) | s[o + 3]


def pack_icon_progress() -> bytes:
    rec = bytearray(32)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0x5A00,
        0x5140,
        0x000A,
        0x6030,
        16,
        194,
        0,
        100,
    )
    struct.pack_into(">HH", rec, 16, 70, 170)
    rec[20] = 26
    rec[21] = 0
    rec[22] = 0
    rec[23] = 255
    rec[24] = 255
    rec[25] = 0
    return bytes(rec)


def _shift_ptrs(s: bytearray, at_or_after: int, delta: int) -> None:
    page_slots = (PAYLOAD - ENTRY0) // 4
    for p in range(page_slots):
        cnt, ptr = get_entry(s, p)
        if ptr >= at_or_after:
            put_entry(s, p, cnt, ptr + delta)


def ensure_progress_icon(s: bytearray) -> None:
    cnt0, ptr0 = get_entry(s, 0)
    if ptr0 != PAYLOAD:
        raise SystemExit(f"unexpected page0 ptr 0x{ptr0:04X}")

    for i in range(cnt0):
        off = ptr0 + i * 32
        if s[off] == 0x5A and s[off + 1] == 0x00:
            if struct.unpack_from(">H", s, off + 6)[0] == 0x6030:
                print(f"  page0 already has IconShow VP6030 @0x{off:04X}")
                return

    if cnt0 != 3:
        raise SystemExit(f"expected page0 cnt=3 (DGUS), got {cnt0}")

    # Insert after the 3 ArtTexts (0x4060 in pristine-with-settings layout).
    insert_at = PAYLOAD + 3 * 32
    icon = pack_icon_progress()
    s[insert_at:insert_at] = icon
    _shift_ptrs(s, insert_at, 32)
    # page0 itself still starts at PAYLOAD; only bump count.
    put_entry(s, 0, 4, PAYLOAD)
    print(f"  inserted IconShow at 0x{insert_at:04X}; shifted ptrs >= insert")


def patch_arttext_widget(s: bytearray, off: int, *, n_int: int, var_type: int, sp: int) -> None:
    """Normalize glyph/color/type so digits are visible and match MCU width."""
    if s[off] != 0x5A or s[off + 1] != 0x03:
        return
    struct.pack_into(">H", s, off + 2, sp & 0xFFFF)
    struct.pack_into(">H", s, off + 12, 30)  # Icon0 = '0' in 24.icl
    s[off + 14] = 24  # Lib
    s[off + 15] = 0
    s[off + 16] = n_int & 0xFF
    s[off + 17] = 0
    s[off + 18] = var_type & 0xFF
    s[off + 19] = 0x01  # right align
    s[off + 20] = 0
    struct.pack_into(">H", s, off + 21, 0xFFFF)  # white (DGUS often leaves 0=black)


# Idle digits in settings wells (layout.json set_val_*). Y nudged up so glyphs sit in the black frame.
SETTINGS_ART_XY = {
    0x6090: (700, 84),   # set_val_brake 500,90,200x50
    0x6094: (700, 173),  # set_val_on    500,180
    0x6096: (700, 263),  # set_val_off   500,270
    0x6098: (700, 354),  # set_val_spd   520,360
}


def patch_settings_xy(s: bytearray) -> int:
    n = 0
    for off in range(PAYLOAD, len(s) - 31, 32):
        if s[off] != 0x5A or s[off + 1] != 0x03:
            continue
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp not in SETTINGS_ART_XY:
            continue
        x, y = SETTINGS_ART_XY[vp]
        ox, oy = struct.unpack_from(">HH", s, off + 8)
        if ox != x or oy != y:
            struct.pack_into(">HH", s, off + 8, x, y)
            n += 1
            print(f"  VP {vp:04X}: xy ({ox},{oy}) -> ({x},{y})")
    return n


def patch_vartypes(s: bytearray) -> int:
    """Fix meter + settings ArtText types/glyphs/colors wherever they live."""
    n = 0
    want = {
        0x6000: (5, ART_VAR_LONG32, 0x5100),
        0x6010: (5, ART_VAR_LONG32, 0x5110),
        0x6020: (2, ART_VAR_UINT16, 0x5120),
        0x6080: (5, ART_VAR_UINT16, 0x5180),
        0x6090: (5, ART_VAR_LONG32, 0x5190),
        0x6094: (4, ART_VAR_UINT16, 0x51A0),
        0x6096: (4, ART_VAR_UINT16, 0x51B0),
        0x6098: (4, ART_VAR_UINT16, 0x51C0),
    }
    for off in range(PAYLOAD, len(s) - 31, 32):
        if s[off] != 0x5A or s[off + 1] != 0x03:
            continue
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp not in want:
            continue
        n_int, vtype, sp = want[vp]
        before = bytes(s[off : off + 32])
        patch_arttext_widget(s, off, n_int=n_int, var_type=vtype, sp=sp)
        if bytes(s[off : off + 32]) != before:
            n += 1
            print(
                f"  VP {vp:04X} @0x{off:04X}: N={n_int} type={vtype} SP={sp:04X} white/Icon0=30"
            )
        else:
            print(f"  VP {vp:04X} @0x{off:04X}: OK")
    return n


def ensure_empty_sentinel(s: bytearray, far: int) -> None:
    need = far + 32
    if len(s) < need:
        s.extend(b"\x00" * (need - len(s)))
    s[far : far + 32] = b"\xff" * 32


def normalize_empty_pages(s: bytearray, keep_pages: set[int], sentinel: int) -> None:
    """Point unused pages at FF sentinel; never at MAIN."""
    if s[9] < 18:
        s[9] = 18
    page_slots = (PAYLOAD - ENTRY0) // 4
    for p in range(page_slots):
        if p in keep_pages:
            continue
        cnt, ptr = get_entry(s, p)
        if cnt == 0:
            put_entry(s, p, 0, sentinel)
        elif ptr == PAYLOAD and p != 0:
            # Non-main page must not share MAIN payload with a positive count
            # unless intentional; leave alone if it has its own widgets.
            pass


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    dset = root / "DWIN_SET"
    out = dset / "14ShowFile.bin"
    pristine = dset / "_from_dgus_generate" / "14ShowFile.bin"
    if not pristine.is_file():
        raise SystemExit(f"missing pristine DGUS show: {pristine}")

    s = bytearray(pristine.read_bytes())
    if s[0] != 0x14:
        raise SystemExit("bad pristine header")

    cnt17, ptr17 = get_entry(s, 17)
    has_settings = cnt17 > 0
    print(f"Base: {pristine} ({len(s)} bytes, max={s[9]}, page17 cnt={cnt17} ptr=0x{ptr17:04X})")

    print("Progress IconShow:")
    ensure_progress_icon(s)

    cnt17, ptr17 = get_entry(s, 17)
    print(f"After icon: page17 cnt={cnt17} ptr=0x{ptr17:04X}")

    print("VarTypes / glyph / color:")
    n = patch_vartypes(s)
    print("Settings ArtText XY (nudge up into wells):")
    n += patch_settings_xy(s)

    # Sentinel after last used payload widget.
    used_end = PAYLOAD
    for p in range((PAYLOAD - ENTRY0) // 4):
        cnt, ptr = get_entry(s, p)
        if cnt > 0:
            used_end = max(used_end, ptr + cnt * 32)
    sentinel = used_end
    # Align to 0x20
    if sentinel % 32:
        sentinel += 32 - (sentinel % 32)
    ensure_empty_sentinel(s, sentinel)
    # Trim trailing junk after sentinel block (keep file tight).
    del s[sentinel + 32 :]

    keep = {0, 10}
    if has_settings and cnt17 > 0:
        keep.add(17)
        print(f"Keeping page17 ArtText @0x{ptr17:04X} cnt={cnt17}")
    else:
        put_entry(s, 17, 0, sentinel)
        print("page17 empty (no DGUS ArtText in base)")

    put_entry(s, 16, 0, sentinel)
    put_entry(s, 18, 0, sentinel)
    normalize_empty_pages(s, keep, sentinel)

    # Re-point empty pages that still share live widget ptrs (layering hygiene).
    for p in range((PAYLOAD - ENTRY0) // 4):
        if p in keep:
            continue
        cnt, ptr = get_entry(s, p)
        if cnt == 0 and ptr != sentinel:
            put_entry(s, p, 0, sentinel)

    bad = [p for p in range((PAYLOAD - ENTRY0) // 4) if get_entry(s, p) == (0, PAYLOAD)]
    if bad:
        raise SystemExit(f"layering hazard: empty pages point at MAIN: {bad[:20]}")

    if 17 in keep:
        c, p = get_entry(s, 17)
        if c < 4:
            raise SystemExit(f"page17 expected >=4 ArtText, got cnt={c}")
        # Verify VPs
        vps = [struct.unpack_from(">H", s, p + i * 32 + 6)[0] for i in range(c)]
        need = {0x6090, 0x6094, 0x6096, 0x6098}
        if not need.issubset(set(vps)):
            raise SystemExit(f"page17 VPs {vps} missing {need - set(vps)}")

    out.write_bytes(s)
    print(f"OK -> {out} (len={len(s)}, patches={n}, sentinel=0x{sentinel:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
