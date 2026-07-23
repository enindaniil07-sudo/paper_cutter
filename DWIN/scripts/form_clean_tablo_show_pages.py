# -*- coding: utf-8 -*-
"""
CLEAN_TABLO show patch — DGUS Save->Generate container is sacred.

Reads pristine from DWIN_SET/_from_dgus_generate/14ShowFile.bin (or current if missing),
writes DWIN_SET/14ShowFile.bin:

1) Force LONG32 on VP 6000/6010 (MCU U32 meters)
2) Insert IconShow VP6030 progress into page0 (shift page10 KB + empty sentinel)
3) Does NOT activate page16 ArtText — that broke layering; brake number needs DGUS Generate

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
    """IconShow 0x5A00 — VP6030 → 26.icl icons 70..170 (same as known-good bak)."""
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


def patch_vartypes(s: bytearray) -> int:
    n = 0
    for off in range(PAYLOAD, len(s) - 31, 32):
        if s[off] != 0x5A or s[off + 1] != 0x03:
            continue
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp in (0x6000, 0x6010):
            if s[off + 18] != ART_VAR_LONG32:
                print(f"  VP {vp:04X} @0x{off:04X}: VarType {s[off + 18]} -> {ART_VAR_LONG32}")
                s[off + 18] = ART_VAR_LONG32
                n += 1
            else:
                print(f"  VP {vp:04X} @0x{off:04X}: LONG32 OK")
        elif vp == 0x6020 and s[off + 18] != ART_VAR_UINT16:
            s[off + 18] = ART_VAR_UINT16
            n += 1
            print(f"  VP 6020 @0x{off:04X}: forced UINT16")
    return n


def ensure_progress_icon(s: bytearray) -> None:
    """Insert IconShow as 4th page0 widget; shift KB + empty by +32."""
    cnt0, ptr0 = get_entry(s, 0)
    if ptr0 != PAYLOAD:
        raise SystemExit(f"unexpected page0 ptr 0x{ptr0:04X}")

    # Already have IconShow on page0?
    for i in range(cnt0):
        off = ptr0 + i * 32
        if s[off] == 0x5A and s[off + 1] == 0x00:
            vp = struct.unpack_from(">H", s, off + 6)[0]
            if vp == 0x6030:
                print(f"  page0 already has IconShow VP6030 @0x{off:04X}")
                return

    if cnt0 != 3:
        raise SystemExit(f"expected page0 cnt=3 (DGUS), got {cnt0}")

    cnt10, ptr10 = get_entry(s, 10)
    if cnt10 != 1 or ptr10 != 0x4060:
        raise SystemExit(f"expected page10 @0x4060 cnt=1, got ptr=0x{ptr10:04X} cnt={cnt10}")

    icon = pack_icon_progress()
    # Expand file by 32 bytes at insert point 0x4060
    insert_at = 0x4060
    s[insert_at:insert_at] = icon  # shifts rest right

    # page0 now 4 contiguous widgets
    put_entry(s, 0, 4, PAYLOAD)

    # page10 KB moved +32
    put_entry(s, 10, 1, 0x4080)

    # DGUS empty-near (pages 1-9) followed page0 end → was 0x4060, now 0x4080
    for p in range(1, 10):
        put_entry(s, p, 0, 0x4080)

    # empty-far / unused (11..max and beyond used slots) was 0x4080 FF, now 0x40A0
    maxp = s[9]
    for p in range(11, max(maxp, 16) + 1):
        put_entry(s, p, 0, 0x40A0)

    # Also fix high directory slots that still say 0x4080 (were empty-far)
    page_slots = (PAYLOAD - ENTRY0) // 4
    for p in range(max(maxp, 16) + 1, page_slots):
        cnt, ptr = get_entry(s, p)
        if cnt == 0 and ptr == 0x4080:
            put_entry(s, p, 0, 0x40A0)

    # Ensure FF sentinel at new empty-far
    if len(s) < 0x40C0:
        s.extend(b"\x00" * (0x40C0 - len(s)))
    s[0x40A0:0x40C0] = b"\xff" * 32

    print("  inserted IconShow VP6030 @0x4060; page10->0x4080; empty-far->0x40A0")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1] / "CLEAN_TABLO")
    args = ap.parse_args()
    root = args.project.resolve()
    dset = root / "DWIN_SET"
    out = dset / "14ShowFile.bin"
    pristine = dset / "_from_dgus_generate" / "14ShowFile.bin"
    if not pristine.is_file():
        raise SystemExit(
            f"missing pristine DGUS show: {pristine}\n"
            "Copy a Save->Generate 14ShowFile.bin there first."
        )

    s = bytearray(pristine.read_bytes())
    if s[0] != 0x14:
        raise SystemExit("bad pristine header")

    print(f"Base: {pristine} ({len(s)} bytes, max={s[9]})")
    print("VarTypes:")
    n = patch_vartypes(s)
    print("Progress IconShow:")
    ensure_progress_icon(s)

    # Hard rule: page16 stays empty (cnt=0) — settings values via touch VarInput
# (adding ArtText here previously broke main/settings layering)
    cnt16, ptr16 = get_entry(s, 16)
    if cnt16 != 0:
        print(f"WARNING: forcing page16 empty (was cnt={cnt16} ptr=0x{ptr16:04X})")
        put_entry(s, 16, 0, 0x40A0)

    out.write_bytes(s)
    print(f"OK -> {out} (len={len(s)}, type_patches={n}, page16 empty)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
