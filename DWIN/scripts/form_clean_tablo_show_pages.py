# -*- coding: utf-8 -*-
"""
CLEAN_TABLO show patch — DGUS Save->Generate container is sacred.

1) Insert Process Bar 0x5A23 VP6030 into page0 (shift later widgets, fix pointers)
2) LONG32/UINT16 + white digits / Icon0=30 on ArtText
3) If DGUS put ArtText on page17 — rewrite settings ArtText
4) page16/18 stay EMPTY at FF-sentinel (never point empty→MAIN 0x4000)
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ENTRY0 = 0x10
PAYLOAD = 0x4000

ART_VAR_LONG32 = 1
ART_VAR_UINT16 = 5

# progress_bar in layout.json
PROGRESS_XS, PROGRESS_YS = 16, 194
PROGRESS_XE, PROGRESS_YE = 783, 215  # x+w-1, y+h-1

VP_ERROR = 0x6040
SP_ERROR = 0x5150
ERROR_ICON_MIN = 80
ERROR_ICON_MAX = 82
ERROR_LIB = 28


def rgb565(r: int, g: int, b: int) -> int:
    return ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)


def put_entry(s: bytearray, page: int, count: int, ptr: int) -> None:
    o = ENTRY0 + page * 4
    s[o] = count & 0xFF
    s[o + 1] = (count >> 8) & 0xFF
    s[o + 2] = (ptr >> 8) & 0xFF
    s[o + 3] = ptr & 0xFF


def get_entry(s: bytes, page: int) -> tuple[int, int]:
    o = ENTRY0 + page * 4
    return s[o] | (s[o + 1] << 8), (s[o + 2] << 8) | s[o + 3]


def pack_process_bar() -> bytes:
    """Process Bar 0x5A23 — VP 6030 0..100, solid fill (no JPEG IconShow glitches)."""
    rec = bytearray(32)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0x5A23,
        0x5140,
        0x000D,
        0x6030,
        PROGRESS_XS,
        PROGRESS_YS,
        PROGRESS_XE,
        PROGRESS_YE,
    )
    struct.pack_into(
        ">HHH",
        rec,
        16,
        rgb565(40, 48, 64),   # border
        rgb565(72, 196, 182), # foreground (aqua)
        rgb565(16, 20, 28),   # background
    )
    struct.pack_into(">hh", rec, 22, 100, 0)  # V_Max, V_Min
    # Mode: show border + fill back, no % return; Direction=Right; integer
    rec[26] = 0x00
    rec[27] = 0x00
    rec[28] = 0x00
    rec[29] = 0x00
    struct.pack_into(">H", rec, 30, 0xFFFF)  # VP_RT unused
    return bytes(rec)


def pack_error_badge(x: int, y: int) -> bytes:
    """IconShow 0x5A00 — VP 6040, 28.icl icons 80..82 (скрыто / энкодер / тормоз)."""
    rec = bytearray(32)
    struct.pack_into(
        ">HHHHHHHH",
        rec,
        0,
        0x5A00,
        SP_ERROR,
        0x000A,
        VP_ERROR,
        x & 0xFFFF,
        y & 0xFFFF,
        0,
        2,
    )
    struct.pack_into(">HH", rec, 16, ERROR_ICON_MIN, ERROR_ICON_MAX)
    rec[20] = ERROR_LIB
    rec[21] = 1  # opaque — same as progress; transparency glitched on panel
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
    n_dot: int = 0,
) -> bytes:
    rec = bytearray(32)
    struct.pack_into(">HHHHHH", rec, 0, 0x5A03, sp & 0xFFFF, 0x0009, vp & 0xFFFF, x & 0xFFFF, y & 0xFFFF)
    struct.pack_into(">H", rec, 12, icon0 & 0xFFFF)
    rec[14] = lib & 0xFF
    rec[15] = 0
    rec[16] = n_int & 0xFF
    rec[17] = n_dot & 0xFF
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


def ensure_progress_bar(s: bytearray) -> None:
    """Ensure page0 has Process Bar VP6030 (replace legacy IconShow if present)."""
    cnt0, ptr0 = get_entry(s, 0)
    if ptr0 != PAYLOAD:
        raise SystemExit(f"unexpected page0 ptr 0x{ptr0:04X}")

    bar = pack_process_bar()
    for i in range(cnt0):
        off = ptr0 + i * 32
        code = struct.unpack_from(">H", s, off)[0]
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp != 0x6030:
            continue
        if code == 0x5A23:
            s[off : off + 32] = bar
            print(f"  page0 Process Bar VP6030 refreshed @0x{off:04X}")
            return
        if code == 0x5A00:
            s[off : off + 32] = bar
            print(f"  page0 IconShow VP6030 → Process Bar @0x{off:04X}")
            return

    if cnt0 != 3:
        raise SystemExit(f"expected page0 cnt=3 (DGUS), got {cnt0}")

    insert_at = PAYLOAD + 3 * 32
    s[insert_at:insert_at] = bar
    _shift_ptrs(s, insert_at, 32)
    put_entry(s, 0, 4, PAYLOAD)
    print(f"  inserted Process Bar at 0x{insert_at:04X}; shifted ptrs >= insert")


def ensure_error_badge(s: bytearray, x: int, y: int) -> None:
    """Ensure page0 has error IconShow VP6040 (popup ! + ЭНКОДЕР/ТОРМОЗ)."""
    cnt0, ptr0 = get_entry(s, 0)
    if ptr0 != PAYLOAD:
        raise SystemExit(f"unexpected page0 ptr 0x{ptr0:04X}")

    icon = pack_error_badge(x, y)
    for i in range(cnt0):
        off = ptr0 + i * 32
        code = struct.unpack_from(">H", s, off)[0]
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp != VP_ERROR:
            continue
        s[off : off + 32] = icon
        print(f"  page0 IconShow VP6040 refreshed @0x{off:04X} xy=({x},{y})")
        return

    insert_at = ptr0 + cnt0 * 32
    s[insert_at:insert_at] = icon
    _shift_ptrs(s, insert_at, 32)
    put_entry(s, 0, cnt0 + 1, PAYLOAD)
    print(f"  inserted error IconShow at 0x{insert_at:04X} xy=({x},{y}); page0 cnt={cnt0 + 1}")


def patch_arttext_widget(
    s: bytearray,
    off: int,
    *,
    n_int: int,
    var_type: int,
    sp: int,
    icon0: int = 30,
    lib: int = 24,
    n_dot: int = 0,
    x: int | None = None,
    y: int | None = None,
    align: int = 0x01,
    icon_mode: int = 0,
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
    s[off + 15] = icon_mode & 0xFF
    s[off + 16] = n_int & 0xFF
    s[off + 17] = n_dot & 0xFF
    s[off + 18] = var_type & 0xFF
    # low 7 bits = TxtAlign (0 left / 1 right / 2 center); bit7 = isShow0
    s[off + 19] = align & 0xFF
    s[off + 20] = 0
    struct.pack_into(">H", s, off + 21, 0xFFFF)


# Settings 25.icl / Icon0=50 (34×52). X = правый край колодца (цифры растут влево).
# Wells: x=460 w=280 → right = 740; Y = верх колодца + 2 (центрирование 52px в 56px).
SETTINGS_ART = (
    (0x5190, 0x6090, 5, ART_VAR_LONG32, 732, 90),
    (0x51A0, 0x6094, 4, ART_VAR_UINT16, 732, 188),
    (0x51B0, 0x6096, 4, ART_VAR_UINT16, 732, 286),
    (0x51C0, 0x6098, 4, ART_VAR_UINT16, 732, 384),  # ПУСТОЙ РУЛОН, мм
)
SETTINGS_VPS = frozenset(vp for _, vp, _, _, _, _ in SETTINGS_ART)
SETTINGS_CNT = len(SETTINGS_ART)

# Крупные цифры 24.icl 56×112, выравнивание вправо: X — правый край колодца.
# Обороты и рулон делят бывшее табло пополам (карточки 378 px).
MAIN_ART_XY = {
    0x6000: (368, 56),
    0x6010: (692, 56),
    0x6020: (369, 252),  # ОБОРОТЫ, левая половина
    0x6024: (759, 252),  # РУЛОН, мм, правая половина
}



def ensure_range_arttext(s: bytearray) -> None:
    """Page 0: ArtText VP 6024 — живое расстояние дальномера, мм."""
    cnt0, ptr0 = get_entry(s, 0)
    if ptr0 != PAYLOAD:
        raise SystemExit(f"unexpected page0 ptr 0x{ptr0:04X}")

    x, y = MAIN_ART_XY[0x6024]
    rec = pack_arttext(0x5130, 0x6024, x, y, n_int=4, var_type=ART_VAR_UINT16, icon0=30, lib=24)
    for i in range(cnt0):
        off = ptr0 + i * 32
        code = struct.unpack_from(">H", s, off)[0]
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if code == 0x5A03 and vp == 0x6024:
            s[off : off + 32] = rec
            print(f"  page0 ArtText VP6024 refreshed @0x{off:04X} xy=({x},{y})")
            return

    insert_at = ptr0 + cnt0 * 32
    s[insert_at:insert_at] = rec
    _shift_ptrs(s, insert_at, 32)
    put_entry(s, 0, cnt0 + 1, PAYLOAD)
    print(f"  inserted range ArtText at 0x{insert_at:04X} xy=({x},{y}); page0 cnt={cnt0 + 1}")


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
    put_entry(s, 17, SETTINGS_CNT, ptr)


def patch_vartypes(s: bytearray) -> int:
    n = 0
    # (n_int, n_dot, var_type, sp, icon0, lib, align, icon_mode)
    # Обороты: XXXX об/мин (N=4.0), right, isShow0 off
    want = {
        0x6000: (5, 0, ART_VAR_LONG32, 0x5100, 30, 24, 0x01, 0),
        0x6010: (5, 0, ART_VAR_LONG32, 0x5110, 30, 24, 0x01, 0),
        0x6020: (4, 0, ART_VAR_UINT16, 0x5120, 30, 24, 0x01, 0),
        0x6024: (4, 0, ART_VAR_UINT16, 0x5130, 30, 24, 0x01, 0),
        0x6080: (5, 0, ART_VAR_UINT16, 0x5180, 30, 24, 0x01, 0),
        0x6090: (5, 0, ART_VAR_LONG32, 0x5190, 50, 25, 0x01, 0),
        0x6094: (4, 0, ART_VAR_UINT16, 0x51A0, 50, 25, 0x01, 0),
        0x6096: (4, 0, ART_VAR_UINT16, 0x51B0, 50, 25, 0x01, 0),
        0x6098: (4, 0, ART_VAR_UINT16, 0x51C0, 50, 25, 0x01, 0),
    }
    for off in range(PAYLOAD, len(s) - 31, 32):
        if s[off] != 0x5A or s[off + 1] != 0x03:
            continue
        vp = struct.unpack_from(">H", s, off + 6)[0]
        if vp not in want:
            continue
        n_int, n_dot, vtype, sp, icon0, lib, align, icon_mode = want[vp]
        xy = MAIN_ART_XY.get(vp)
        before = bytes(s[off : off + 32])
        patch_arttext_widget(
            s,
            off,
            n_int=n_int,
            n_dot=n_dot,
            var_type=vtype,
            sp=sp,
            icon0=icon0,
            lib=lib,
            x=None if xy is None else xy[0],
            y=None if xy is None else xy[1],
            align=align,
            icon_mode=icon_mode,
        )
        if bytes(s[off : off + 32]) != before:
            n += 1
            extra = f" xy={xy}" if xy else ""
            print(
                f"  VP {vp:04X} @0x{off:04X}: N={n_int}.{n_dot} type={vtype} "
                f"lib={lib} icon0={icon0} align=0x{align:02X} mode={icon_mode}{extra}"
            )
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

    print("Progress Process Bar:")
    ensure_progress_bar(s)

    layout_path = root / "design" / "layout.json"
    err = {"x": 714, "y": 80}
    if layout_path.is_file():
        err = (json.loads(layout_path.read_text(encoding="utf-8")).get("controls") or {}).get(
            "error_badge"
        ) or err
    print("Error badge IconShow:")
    ensure_error_badge(s, int(err["x"]), int(err["y"]))

    print("Range ArtText:")
    ensure_range_arttext(s)

    cnt17, ptr17 = get_entry(s, 17)
    print(f"After icon: page17 cnt={cnt17} ptr=0x{ptr17:04X}")

    if has_settings and cnt17 > 0:
        print("Settings ArtText page17:")
        rewrite_settings_page17(s, ptr17)
        cnt17, ptr17 = get_entry(s, 17)

    print("VarTypes / glyph / color:")
    n = patch_vartypes(s)

    used_end = PAYLOAD
    for p in range((PAYLOAD - ENTRY0) // 4):
        cnt, ptr = get_entry(s, p)
        if cnt > 0:
            used_end = max(used_end, ptr + cnt * 32)
    # Include cleared slot after settings widgets (SETTINGS_CNT ArtText).
    if has_settings:
        used_end = max(used_end, ptr17 + (SETTINGS_CNT + 1) * 32)
    sentinel = used_end
    if sentinel % 32:
        sentinel += 32 - (sentinel % 32)
    ensure_empty_sentinel(s, sentinel)
    del s[sentinel + 32 :]

    keep = {0, 10}
    if has_settings and cnt17 == SETTINGS_CNT:
        keep.add(17)
        print(f"Keeping page17 settings @0x{ptr17:04X} cnt={SETTINGS_CNT}")
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
        if c != SETTINGS_CNT or set(vps) != SETTINGS_VPS:
            raise SystemExit(f"page17 bad: cnt={c} vps={vps}")

    out.write_bytes(s)
    print(f"OK -> {out} (len={len(s)}, patches={n}, sentinel=0x{sentinel:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
