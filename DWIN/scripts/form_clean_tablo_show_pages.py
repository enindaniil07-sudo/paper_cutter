# -*- coding: utf-8 -*-
"""
CLEAN_TABLO show patch — DGUS Save->Generate container is sacred.

1) Insert IconShow VP6030 into page0 (shift later widgets, fix pointers)
2) LONG32/UINT16 + white digits / Icon0=30 on ArtText
3) If DGUS put ArtText on page17 — rewrite to 3 settings (no speed limit)
4) page16/18 stay EMPTY at FF-sentinel (never point empty→MAIN 0x4000)
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


def pack_arttext(
    sp: int,
    vp: int,
    x: int,
    y: int,
    *,
    n_int: int,
    var_type: int,
    icon0: int = 30,
    lib: int = 24,
) -> bytes:
    rec = bytearray(32)
    struct.pack_into(">HHHHHH", rec, 0, 0x5A03, sp & 0xFFFF, 0x0009, vp & 0xFFFF, x & 0xFFFF, y & 0xFFFF)
    struct.pack_into(">H", rec, 12, icon0 & 0xFFFF)
    rec[14] = lib & 0xFF
    rec[15] = 0
    rec[16] = n_int & 0xFF
    rec[17] = 0
    rec[18] = var_type & 0xFF
    rec[19] = 0x01
    rec[20] = 0
    struct.pack_into(">H", rec, 21, 0xFFFF)
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

    insert_at = PAYLOAD + 3 * 32
    s[insert_at:insert_at] = pack_icon_progress()
    _shift_ptrs(s, insert_at, 32)
    put_entry(s, 0, 4, PAYLOAD)
    print(f"  inserted IconShow at 0x{insert_at:04X}; shifted ptrs >= insert")


def patch_arttext_widget(
    s: bytearray,
    off: int,
    *,
    n_int: int,
    var_type: int,
    sp: int,
    icon0: int = 30,
    lib: int = 24,
    x: int | None = None,
    y: int | None = None,
) -> None:
    if s[off] != 0x5A or s[off + 1] != 0x03:
        return
    struct.pack_into(">H", s, off + 2, sp & 0xFFFF)
    if x is not None:
        struct.pack_into(">H", s, off + 8, x & 0xFFFF)
    if y is not None:
        struct.pack_into(">H", s, off + 10, y & 0xFFFF)
    struct.pack_into(">H", s, off + 12, icon0 & 0xFFFF)
    s[off + 14] = lib & 0xFF
    s[off + 15] = 0
    s[off + 16] = n_int & 0xFF
    s[off + 17] = 0
    s[off + 18] = var_type & 0xFF
    s[off + 19] = 0x01
    s[off + 20] = 0
    struct.pack_into(">H", s, off + 21, 0xFFFF)


# Settings use 25.icl / Icon0=50 (34×52 cells) — larger than 28×36, tighter pitch than 56×72.
# X=720 = right edge of large wells; Y centered in value wells for 52px glyphs.
SETTINGS_ART = (
    (0x5190, 0x6090, 5, ART_VAR_LONG32, 720, 105),
    (0x51A0, 0x6094, 4, ART_VAR_UINT16, 720, 229),
    (0x51B0, 0x6096, 4, ART_VAR_UINT16, 720, 353),
)

# Main ЗАДАНО / ОСТАЛОСЬ: Y = top of 112px wells (glyphs are 56×112).
# Speed well is only 72px tall — nudge Y up so 112px glyphs sit in the rim.
MAIN_ART_XY = {
    0x6000: (368, 56),
    0x6010: (692, 56),
    0x6020: (760, 248),
}


def rewrite_settings_page17(s: bytearray, ptr: int) -> None:
    blob = bytearray()
    for sp, vp, n_int, vtype, x, y in SETTINGS_ART:
        blob += pack_arttext(sp, vp, x, y, n_int=n_int, var_type=vtype, icon0=50, lib=25)
        print(f"  ArtText VP {vp:04X} @({x},{y}) N={n_int} type={vtype} lib=25 icon0=50")
    need = ptr + len(blob) + 32
    if len(s) < need:
        s.extend(b"\x00" * (need - len(s)))
    s[ptr : ptr + len(blob)] = blob
    s[ptr + len(blob) : ptr + len(blob) + 32] = b"\xff" * 32
    put_entry(s, 17, 3, ptr)


def patch_vartypes(s: bytearray) -> int:
    n = 0
    # (n_int, var_type, sp, icon0, lib)
    want = {
        0x6000: (5, ART_VAR_LONG32, 0x5100, 30, 24),
        0x6010: (5, ART_VAR_LONG32, 0x5110, 30, 24),
        0x6020: (2, ART_VAR_UINT16, 0x5120, 30, 24),
        0x6080: (5, ART_VAR_UINT16, 0x5180, 30, 24),
        0x6090: (5, ART_VAR_LONG32, 0x5190, 50, 25),
        0x6094: (4, ART_VAR_UINT16, 0x51A0, 50, 25),
        0x6096: (4, ART_VAR_UINT16, 0x51B0, 50, 25),
    }
    for off in range(PAYLOAD, len(s) - 31, 32):
        if s[off] != 0x5A or s[off + 1] != 0x03:
            continue
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp not in want:
            continue
        n_int, vtype, sp, icon0, lib = want[vp]
        xy = MAIN_ART_XY.get(vp)
        before = bytes(s[off : off + 32])
        patch_arttext_widget(
            s,
            off,
            n_int=n_int,
            var_type=vtype,
            sp=sp,
            icon0=icon0,
            lib=lib,
            x=None if xy is None else xy[0],
            y=None if xy is None else xy[1],
        )
        if bytes(s[off : off + 32]) != before:
            n += 1
            extra = f" xy={xy}" if xy else ""
            print(f"  VP {vp:04X} @0x{off:04X}: N={n_int} type={vtype} lib={lib} icon0={icon0}{extra}")
        else:
            print(f"  VP {vp:04X} @0x{off:04X}: OK")
    return n


def ensure_empty_sentinel(s: bytearray, far: int) -> None:
    need = far + 32
    if len(s) < need:
        s.extend(b"\x00" * (need - len(s)))
    s[far : far + 32] = b"\xff" * 32


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

    if has_settings and cnt17 > 0:
        print("Settings ArtText page17 (3 params, large wells):")
        rewrite_settings_page17(s, ptr17)
        cnt17, ptr17 = get_entry(s, 17)

    print("VarTypes / glyph / color:")
    n = patch_vartypes(s)

    used_end = PAYLOAD
    for p in range((PAYLOAD - ENTRY0) // 4):
        cnt, ptr = get_entry(s, p)
        if cnt > 0:
            used_end = max(used_end, ptr + cnt * 32)
    # Include cleared 4th slot after settings widgets.
    if has_settings:
        used_end = max(used_end, ptr17 + 4 * 32)
    sentinel = used_end
    if sentinel % 32:
        sentinel += 32 - (sentinel % 32)
    ensure_empty_sentinel(s, sentinel)
    del s[sentinel + 32 :]

    keep = {0, 10}
    if has_settings and cnt17 == 3:
        keep.add(17)
        print(f"Keeping page17 ArtText @0x{ptr17:04X} cnt=3")
    else:
        put_entry(s, 17, 0, sentinel)
        print("page17 empty (no DGUS ArtText in base)")

    put_entry(s, 16, 0, sentinel)
    put_entry(s, 18, 0, sentinel)

    page_slots = (PAYLOAD - ENTRY0) // 4
    if s[9] < 18:
        s[9] = 18
    for p in range(page_slots):
        if p in keep:
            continue
        cnt, ptr = get_entry(s, p)
        if cnt == 0:
            put_entry(s, p, 0, sentinel)

    bad = [p for p in range(page_slots) if get_entry(s, p) == (0, PAYLOAD)]
    if bad:
        raise SystemExit(f"layering hazard: empty pages point at MAIN: {bad[:20]}")

    if 17 in keep:
        c, p = get_entry(s, 17)
        vps = [struct.unpack_from(">H", s, p + i * 32 + 6)[0] for i in range(c)]
        if c != 3 or set(vps) != {0x6090, 0x6094, 0x6096}:
            raise SystemExit(f"page17 bad: cnt={c} vps={vps}")

    out.write_bytes(s)
    print(f"OK -> {out} (len={len(s)}, patches={n}, sentinel=0x{sentinel:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
