# -*- coding: utf-8 -*-
"""
Build T5LCFG.CFG (T5L DGUS II hardware configuration) to match DGUS behavior.

Official DGUS V7.x copies Application.StartupPath\\Config\\T5LCFG.CFG and patches byte 5
for portrait/landscape (see Disassembler/SRS/DGUS_V7.649/Form1.cs myButton128_Click).

Usage:
  # Closest to official: copy stock file from DGUS install, then patch fields
  python t5lcfg_builder.py -o T5LCFG.CFG --reference "C:/.../DGUS_V7649/Config/T5LCFG.CFG" --rotation 0

  # From scratch using manual table values (verify on hardware)
  python t5lcfg_builder.py -o T5LCFG.CFG --preset 800x480 --baud 115200 --rotation 0

Layout follows DWIN T5L / DGUS II application guides in this repo (DWIN_COMPREHENSIVE_MANUAL.md):
  0x00..0x04  magic T5LC1 (0x54 0x35 0x4C 0x43 0x31)
  0x05        system bits (UART CRC, buzzer/music, 22 load, touch upload, sound, backlight, rotation)
  0x10..0x11  display config enable 0x5A 0xA5
  0x12..0x1F  LCD timing row from DWIN tables
  ...
Observed stock files may be 114 bytes (0x72). Clone mode preserves reference length exactly.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

T5LCFG_MAGIC = b"T5LC1"
T5LCFG_SIZE = 0x72

# Display timing presets: 14 bytes for file offsets 0x12..0x1F (from DWIN tables).
DISPLAY_PRESETS: dict[str, bytes] = {
    "800x480": bytes.fromhex("01 06 1E 10 03 20 D2 03 14 01 E0 0C 00 00"),
    "1024x600": bytes.fromhex("01 04 A0 88 04 00 18 06 1D 02 58 03 00 00"),
    "480x272": bytes.fromhex("01 16 29 02 01 E0 02 0A 02 01 10 02 00 00"),
    "640x480": bytes.fromhex("01 08 1E 72 02 58 10 03 20 01 E0 0A 00 00"),
    "320x480": bytes.fromhex("01 14 0A 04 01 40 0A 02 02 01 E0 02 04 00"),
    "480x480": bytes.fromhex("00 0E 08 08 01 E0 08 02 0C 01 E0 06 08 00"),
}

_BAUD_NUMERATOR = 3225600


def baud_to_bytes(baud: int) -> tuple[int, int]:
    """UART divisor at 0x0A..0x0B big-endian (see DWIN manual)."""
    if baud <= 0:
        raise ValueError("baud must be positive")
    v = round(_BAUD_NUMERATOR / baud)
    if v < 1 or v > 0x03FF:
        raise ValueError(f"baud divisor {v} out of range 1..0x3FF for this formula")
    return (v >> 8) & 0xFF, v & 0xFF


def set_rotation(cfg: bytearray, degrees: int) -> None:
    """Bits 1..0 of byte 0x05: 00=0°, 01=90°, 10=180°, 11=270°."""
    m = {0: 0, 90: 1, 180: 2, 270: 3}
    if degrees not in m:
        raise ValueError("rotation must be 0, 90, 180, or 270")
    cfg[0x05] = (cfg[0x05] & 0xFC) | m[degrees]


def set_uart_crc(cfg: bytearray, enabled: bool) -> None:
    """Bit 7 of byte 0x05: UART CRC-16 Modbus on DGUS frames."""
    if enabled:
        cfg[0x05] |= 0x80
    else:
        cfg[0x05] &= ~0x80


def apply_display_row(cfg: bytearray, row14: bytes) -> None:
    if len(row14) != 14:
        raise ValueError("display row must be 14 bytes")
    cfg[0x12 : 0x20] = row14


def build_from_scratch(
    *,
    preset: str,
    baud: int,
    rotation: int,
    byte05_base: int | None = None,
) -> bytes:
    """
    Minimal factory-like template. Touch/buzzer blocks use common demo values;
    validate against your module datasheet.
    """
    if preset not in DISPLAY_PRESETS:
        keys = ", ".join(sorted(DISPLAY_PRESETS))
        raise ValueError(f"Unknown preset {preset!r}. Choose: {keys}")

    cfg = bytearray(T5LCFG_SIZE)
    cfg[0:5] = T5LCFG_MAGIC

    # Byte 0x05 default: example from manual (0x38): no UART CRC, buzzer, load 22 on,
    # touch auto upload on, touch sound on, standby backlight off, rotation 0° in low bits.
    cfg[0x05] = byte05_base if byte05_base is not None else 0x38
    set_rotation(cfg, rotation)

    cfg[0x06] = 0x00
    cfg[0x07] = 0x00  # WAE id
    cfg[0x08] = 0x10  # background / ICL storage band (manual example)
    cfg[0x09] = 0x28  # touch report rate factory default
    hi, lo = baud_to_bytes(baud)
    cfg[0x0A] = hi
    cfg[0x0B] = lo
    cfg[0x0C] = 0x64  # brightness 100%
    cfg[0x0D] = 0x64  # standby brightness
    cfg[0x0E] = 0x00
    cfg[0x0F] = 0x3C  # standby resume (example)

    cfg[0x10] = 0x5A
    cfg[0x11] = 0xA5
    apply_display_row(cfg, DISPLAY_PRESETS[preset])

    # Touch: enable block + GT911 class (0x1n) — common on DWIN CTP demos.
    cfg[0x20] = 0x5A
    cfg[0x21] = 0x10
    cfg[0x22] = 0x14
    cfg[0x23] = 0x00

    # Buzzer factory-ish (manual example 2.5 kHz, 8% duty)
    cfg[0x27] = 0x5A
    cfg[0x28] = 0x6E
    cfg[0x29] = 0x0B
    cfg[0x2A] = 0xB8
    cfg[0x2B] = 0x00
    cfg[0x2C] = 0xF0
    cfg[0x2D] = 0x0A

    cfg[0x2E] = 0x5A
    cfg[0x2F] = 0x16  # init file id 22.bin family

    cfg[0x30] = 0x5A
    cfg[0x31] = 0x00  # sysclock adj

    cfg[0x32] = 0x00
    cfg[0x33] = 0x00
    cfg[0x34] = 0x00
    cfg[0x35] = 0x00
    cfg[0x36] = 0x00

    # 0x37..0x3F reserved (9) — already zero
    # 0x40 SD / encryption — leave zero
    # 0x4B..0x6F reserved — zero
    cfg[0x70] = 0x5A
    cfg[0x71] = 0x0A
    # 0x72..0x7F reserved

    return bytes(cfg)


def clone_and_patch(
    reference: Path,
    *,
    rotation: int | None = None,
    baud: int | None = None,
    preset: str | None = None,
    uart_crc: bool | None = None,
) -> bytes:
    """Copy stock DGUS T5LCFG.CFG and optionally patch fields (matches tool workflow)."""
    data = reference.read_bytes()
    if len(data) < 0x20:
        raise ValueError("Reference file too small to be T5LCFG.CFG")
    if data[0:5] != T5LCFG_MAGIC:
        sys.stderr.write(
            "Warning: reference does not start with T5LC1 magic; copying anyway.\n"
        )
    cfg = bytearray(data)

    if rotation is not None:
        set_rotation(cfg, rotation)
    if baud is not None:
        hi, lo = baud_to_bytes(baud)
        cfg[0x0A] = hi
        cfg[0x0B] = lo
    if preset is not None:
        if preset not in DISPLAY_PRESETS:
            raise ValueError(f"Unknown preset {preset!r}")
        apply_display_row(cfg, DISPLAY_PRESETS[preset])
        cfg[0x10] = 0x5A
        cfg[0x11] = 0xA5
    if uart_crc is not None:
        set_uart_crc(cfg, uart_crc)

    return bytes(cfg)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build or patch T5LCFG.CFG for DWIN T5L DGUS II")
    ap.add_argument("-o", "--output", type=Path, required=True, help="Output T5LCFG.CFG path")
    ap.add_argument(
        "-r",
        "--reference",
        type=Path,
        default=None,
        help="Stock T5LCFG.CFG from DGUS Config folder (recommended)",
    )
    ap.add_argument(
        "--preset",
        type=str,
        default=None,
        help=f"LCD table preset: {', '.join(sorted(DISPLAY_PRESETS))} (from-scratch or overlay on reference)",
    )
    ap.add_argument(
        "--rotation",
        type=int,
        choices=(0, 90, 180, 270),
        default=None,
        help="Display rotation (sets bits 1..0 of byte 0x05)",
    )
    ap.add_argument(
        "--baud",
        type=int,
        default=None,
        help="UART baud rate (e.g. 115200); writes divisor at 0x0A..0x0B",
    )
    ap.add_argument(
        "--from-scratch",
        action="store_true",
        help="Ignore --reference; build template (requires --preset)",
    )
    crc_grp = ap.add_mutually_exclusive_group()
    crc_grp.add_argument(
        "--crc",
        action="store_true",
        help="Enable UART CRC (set bit 7 of byte 0x05)",
    )
    crc_grp.add_argument(
        "--no-crc",
        action="store_true",
        help="Disable UART CRC (clear bit 7 of byte 0x05)",
    )
    args = ap.parse_args()

    try:
        if args.from_scratch:
            if not args.preset:
                ap.error("--from-scratch requires --preset")
            out = build_from_scratch(
                preset=args.preset,
                baud=args.baud or 115200,
                rotation=args.rotation if args.rotation is not None else 0,
            )
        elif args.reference:
            uart_crc = None
            if args.crc:
                uart_crc = True
            elif args.no_crc:
                uart_crc = False
            out = clone_and_patch(
                args.reference,
                rotation=args.rotation,
                baud=args.baud,
                preset=args.preset,
                uart_crc=uart_crc,
            )
        else:
            ap.error("Provide --reference (recommended) or --from-scratch with --preset")

        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(out)
        print(f"Wrote {args.output} ({len(out)} bytes)")
    except Exception as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
