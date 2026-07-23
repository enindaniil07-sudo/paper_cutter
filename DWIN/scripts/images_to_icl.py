# -*- coding: utf-8 -*-
"""
Pack one or more raster images into a DGUS-style .icl library (DGUS_3 container).

Reverse-engineered to match BizDraw.Utils.ICL in DwinTerminal / Excel2ICL (JPEG records,
32 KiB header+index base, CRC16 over bytes from offset 8).

Requires: pip install Pillow

Example:
  python images_to_icl.py                    # GUI: tabs Pack images | Preview .icl
  python images_to_icl.py --gui
  python images_to_icl.py --preview 48.icl   # list icons + JPEG sizes (CLI)
  Preview tab: Browse .icl → Load & show icons (double-click thumbnail to enlarge).
  Pack tab: "Open .icl in new window…" for a popup preview.
  python images_to_icl.py -o 48.icl --icl-id 48 photo1.png photo2.jpg
  python images_to_icl.py -o icons.icl --quality 85 *.png

Icon ids in the file follow leading digits in each image filename (e.g. 32.png -> id 32),
like DGUS Config / DwinTerminal. Names without digits use 0, 1, 2, … in list order.

Same BMP in DGUS vs this tool: the official app encodes JPEG with its own codec/settings;
we re-encode with Pillow, so sizes differ. To match DGUS closely, save a .jpg from the
official tool (or export intermediate images) and enable “Keep JPEG as-is” for .jpeg inputs.

DGUS Config-style options (see GUI / run_pack / CLI) map loosely as:
  Align head/KB  -> align_head_kb (0 = no pad, payload after index; default 32 like ICL.cs)
  Lib size       -> lib_size_kb 256 or 512 (flash stride for bank-cross zero fill)
  JPG quality %  -> quality
  4:4:4 / 4:1:1  -> chroma (Pillow subsampling; 411 uses 4:2:0 as closest)
  Align body/KB  -> not stored in decompiled Excel2ICL ICL.cs (JPEG “align” tail optional)
  Core T5L0/1/2  -> not encoded in .icl bytes we parse; use the correct tool chain for download

Disclaimer: DWIN does not publish this format; test on hardware.
"""

from __future__ import annotations

import argparse
import base64
import io
import re
import sys
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

# DGUS ICL: header + index padded to this offset, then JPEG records (dense index).
_ICL_START_INDEX = 32768

# CRC tables from Disassembler/SRS/DwinTerminal/Utils/CRC16.cs (512 bytes)
_CRC_B64 = (
    "AMGBQAHAgEEBwIBBAMGBQAHAgEEAwYFAAMGBQAHAgEEBwIBBAMGBQADBgUABwIBBAMGBQAHAgEEBwIB"
    "BAMGBQAHAgEEAwYFAAMGBQAHAgEEAwYFAAcCAQQHAgEEAwYFAAMGBQAHAgEEBwIBBAMGBQAHAgEEAwYF"
    "AAMGBQAHAgEEBwIBBAMGBQADBgUABwIBBAMGBQAHAgEEBwIBBAMGBQADBgUABwIBBAcCAQQDBgUABwIB"
    "BAMGBQADBgUABwIBBAMGBQAHAgEEBwIBBAMGBQAHAgEEAwYFAAMGBQAHAgEEBwIBBAMGBQADBgUABwIB"
    "BAMGBQAHAgEEBwIBBAMGBQADAwQHDAwLCxgYHxwXFxATMDA3ND8/ODgrKywvJCQjI2BgZ2Rvb2hoe3t8"
    "f3R0c3BTU1RXXFxbW0hIT0xHR0BDwMDHxM/PyMjb29zf1NTT0PPz9Pf8/Pv76Ojv7Ofn4OCjo6SnrKyr"
    "q7i4v7y3t7CzkJCXlJ+fmJiLi4yPhISDgoGBhoWOjomJmpqdnpWVkpGysrW2vb26uqmprq2mpqGh4uLl"
    "5u3t6ur5+f799vbx8tHR1tXe3tnZysrNzsXFwsFCQkVGTU1KSllZXl1WVlFScXF2dX5+eXlqam1uZWVi"
    "YiEhJiUuLikpOjo9PjU1MjESEhUWHR0aGkkJDg0GBgEA="
)


def _crc_tables() -> tuple[bytes, bytes]:
    raw = base64.b64decode(_CRC_B64)
    if len(raw) != 512:
        raise RuntimeError("embedded CRC tables corrupted")
    return raw[:256], raw[256:]


def crc16_dwin(data: bytes, begin: int = 8) -> bytes:
    """Match CRC16.CalculateCrc16(buffer, begin) -> [hi, lo] written to file[6:8]."""
    hi_tbl, lo_tbl = _crc_tables()
    num = 255
    maxv = 255
    for i in range(begin, len(data)):
        idx = num ^ data[i]
        num = maxv ^ hi_tbl[idx]
        maxv = lo_tbl[idx]
    return bytes([num, maxv])


def get_bytes_be(val: int, length: int) -> bytes:
    return int(val).to_bytes(length, "big", signed=False)


def icl_numeric_id_from_filename(name: str) -> int:
    """Leading digits of stem (matches BizDraw.Utils.Utils.getId / ICL icon filenames)."""
    m = re.match(r"^(\d+)", Path(name).stem)
    if not m:
        return -1
    return int(m.group(1))


def assign_icon_ids_from_paths(paths: list[Path]) -> list[tuple[int, Path]]:
    """
    Icon id = leading digits in filename (e.g. 32.png -> 32), same as DGUS icon folder.
    Names without a leading number get the smallest unused non-negative integers (list order).
    """
    by_explicit: dict[int, Path] = {}
    for p in paths:
        pid = icl_numeric_id_from_filename(p.name)
        if pid >= 0:
            if pid in by_explicit:
                raise ValueError(
                    f"Duplicate icon id {pid}: {by_explicit[pid].name} and {p.name}"
                )
            by_explicit[pid] = p
    used = set(by_explicit)
    next_auto = 0
    out: list[tuple[int, Path]] = []
    for p in paths:
        pid = icl_numeric_id_from_filename(p.name)
        if pid >= 0:
            out.append((pid, p))
        else:
            while next_auto in used:
                next_auto += 1
            out.append((next_auto, p))
            used.add(next_auto)
            next_auto += 1
    return out


def find_jpeg_sos(data: bytearray) -> int:
    """First 0xFF 0xDA (start of scan). Same scan as decompiled findFileHead."""
    file_head = -1
    while True:
        try:
            file_head = data.index(0xFF, file_head + 1)
        except ValueError:
            return -1
        if file_head + 1 < len(data) and data[file_head + 1] == 0xDA:
            return file_head


def chroma_to_subsampling(chroma: str) -> int | None:
    """
    Map DGUS “Sample type” to Pillow JPEG subsampling.
    Pillow: 0 = 4:4:4, 1 = 4:2:2, 2 = 4:2:0. True JPEG 4:1:1 is not separate in Pillow;
    we use 4:2:0 (2) as the closest strong chroma reduction for “4:1:1”.
    """
    c = chroma.strip().lower().replace(" ", "")
    if c in ("444", "4:4:4"):
        return 0
    if c in ("411", "4:1:1", "420", "4:2:0"):
        return 2
    if c in ("422", "4:2:2"):
        return 1
    raise ValueError(f"Unknown chroma '{chroma}' (use 444, 411, 422)")


_JPEG_SUFFIX = {".jpg", ".jpeg", ".jpe"}


def image_to_jpeg_bytes(
    path: Path,
    quality: int,
    *,
    subsampling: int | None = None,
) -> tuple[int, int, bytes]:
    from PIL import Image

    im = Image.open(path)
    im = im.convert("RGB")
    w, h = im.size
    buf = io.BytesIO()
    kw: dict = {"format": "JPEG", "quality": quality}
    if subsampling is not None:
        kw["subsampling"] = subsampling
    im.save(buf, **kw)
    return w, h, buf.getvalue()


def load_image_jpeg_for_icl(
    path: Path,
    quality: int,
    subsampling: int | None,
    *,
    keep_jpeg_raw: bool,
) -> tuple[int, int, bytes]:
    """
    Width/height + JPEG bytes for packing. If keep_jpeg_raw and path is .jpg/.jpeg,
    embed file bytes unchanged (matches DGUS if you use the same JPG the tool would embed).
    Otherwise decode (BMP/PNG/…) and re-encode with Pillow — different encoder ⇒ different size
    than the official app even from the same source bitmap.
    """
    path = Path(path)
    if keep_jpeg_raw:
        if path.suffix.lower() not in _JPEG_SUFFIX:
            raise ValueError(
                f"“Keep JPEG as-is” only applies to .jpg/.jpeg files, not {path.suffix!r}. "
                "Use a JPEG exported from DGUS, or leave the option off for BMP/PNG."
            )
        raw = path.read_bytes()
        if not raw.startswith(b"\xff\xd8"):
            raise ValueError(f"Not a baseline JPEG file: {path.name}")
        from PIL import Image

        im = Image.open(io.BytesIO(raw))
        im = im.convert("RGB")
        w, h = im.size
        return w, h, raw
    return image_to_jpeg_bytes(path, quality, subsampling=subsampling)


def build_icon_record(width: int, height: int, jpeg: bytes) -> bytearray | None:
    """One ICL payload record; returns None if JPEG SOS not found."""
    byte_list = bytearray()
    byte_list.extend(get_bytes_be(width, 2))
    byte_list.extend(get_bytes_be(height, 2))
    byte_list.extend(get_bytes_be(0, 6))
    byte_list.extend(jpeg)

    file_head = find_jpeg_sos(byte_list)
    if file_head == -1:
        return None

    index1 = file_head + 2 + 12
    rem = index1 % 4
    if rem > 0:
        num6 = 4 - rem
        for _ in range(num6):
            byte_list.insert(index1, 0xFF)
        index1 += num6
        byte_list[file_head + 3] = (num6 + byte_list[file_head + 3]) & 0xFF

    byte_list.extend(bytes(8))
    val = len(byte_list) - index1
    rem7 = val % 4
    if rem7 > 0:
        num8 = 4 - rem7
        byte_list.extend(bytes(num8))
        val += num8

    byte_list[4] = get_bytes_be(index1 - 10, 2)[0]
    byte_list[5] = get_bytes_be(index1 - 10, 2)[1]
    b2 = get_bytes_be(val, 4)
    byte_list[6] = b2[0]
    byte_list[7] = b2[1]
    byte_list[8] = b2[2]
    byte_list[9] = b2[3]
    return byte_list


def pack_icl(
    icons: list[tuple[int, int, int, bytes, int]],
    *,
    icl_slot_id: int,
    align_head_kb: int = 32,
    lib_size_kb: int = 256,
) -> bytes:
    """
    icons: (icon_id, width, height, jpeg_bytes, span_len) per slot.
    span_len is the on-disk source file size for bank-cross padding (matches ICL.cs Fi.Length).
    Missing ids between 0 and max(icon_id) get index offset 0 (sparse library), like DwinTerminal.

    align_head_kb: DGUS “Align head/KB”. 0 = tight (payload starts right after index); else pad
    start of body to max(header_len, align_head_kb * 1024) like default 32 KiB in ICL.cs.
    lib_size_kb: DGUS “Lib size” 256 or 512 — stride for flash bank padding (262144 or 524288).
    """
    by_id = {i: (w, h, j, sp) for i, w, h, j, sp in icons}
    max_id = max(by_id)
    header_len = 16 + (max_id + 1) * 4
    if align_head_kb > 0:
        start_index = max(header_len, align_head_kb * 1024)
    else:
        start_index = header_len
    slot_span = lib_size_kb * 1024
    if lib_size_kb not in (256, 512):
        raise ValueError("lib_size_kb must be 256 or 512")

    collection1 = bytearray()
    collection2 = bytearray()

    for key in range(max_id + 1):
        if key not in by_id:
            collection1.extend(get_bytes_be(0, 4))
            continue
        w, h, jpeg, span_len = by_id[key]

        num1 = start_index + len(collection2)
        num2 = icl_slot_id * slot_span + num1
        num3 = num2 + span_len - 1
        if (num2 >> 24) != (num3 >> 24):
            num4 = (num3 & 0xFF000000) - icl_slot_id * slot_span
            if num4 > num1:
                collection2.extend(bytes(int(num4 - num1)))

        collection1.extend(get_bytes_be(start_index + len(collection2), 4))
        rec = build_icon_record(w, h, jpeg)
        if rec is None:
            raise ValueError(f"icon id {key}: invalid JPEG (no SOS marker)")
        collection2.extend(rec)

    byte_list1 = bytearray()
    byte_list1.extend(b"DGUS_3\x00\x00")
    byte_list1.extend(get_bytes_be(4096 + len(collection2) - 8, 4))
    byte_list1.append(4)
    byte_list1.extend(get_bytes_be(max_id, 2))
    byte_list1.append(0)
    byte_list1.extend(collection1)

    pad = start_index - len(byte_list1)
    if pad < 0:
        raise RuntimeError(
            "Negative pad: index + header larger than payload start; increase Align head/KB"
        )
    byte_list1.extend(bytes(pad))
    byte_list1.extend(collection2)

    crc = crc16_dwin(bytes(byte_list1), 8)
    byte_list1[6] = crc[0]
    byte_list1[7] = crc[1]
    return bytes(byte_list1)


def pack_icl_dense(
    icons: list[tuple[int, int, int, bytes]],
    *,
    icl_slot_id: int,
    align_head_kb: int = 32,
    lib_size_kb: int = 256,
) -> bytes:
    """Dense ids 0..N-1: span_len uses encoded JPEG length (no source path)."""
    with_span: list[tuple[int, int, int, bytes, int]] = [
        (i, w, h, j, len(j)) for i, w, h, j in icons
    ]
    by_id = {i: (w, h, j, sp) for i, w, h, j, sp in with_span}
    max_id = max(by_id)
    for i in range(max_id + 1):
        if i not in by_id:
            raise ValueError(f"dense ICL requires every id 0..{max_id}; missing {i}")
    return pack_icl(
        with_span,
        icl_slot_id=icl_slot_id,
        align_head_kb=align_head_kb,
        lib_size_kb=lib_size_kb,
    )


def _repair_dwin_jpeg_sos_stuffing(data: bytearray) -> None:
    """
    DWIN ICL packing inserts 0xFF alignment bytes after the SOS header and increases
    the SOS segment length (see BizDraw.Utils.ICL.findFileHead / editImage). Standard
    decoders fail unless those bytes are removed and Ls is corrected.
    """
    fh = -1
    for i in range(len(data) - 1):
        if data[i] == 0xFF and data[i + 1] == 0xDA:
            fh = i
            break
    if fh < 0 or fh + 15 > len(data):
        return
    old_ls = (data[fh + 2] << 8) | data[fh + 3]
    count = data[fh + 3] - 12
    if count <= 0 or fh + 14 + count > len(data):
        return
    del data[fh + 14 : fh + 14 + count]
    new_ls = old_ls - count
    data[fh + 2] = (new_ls >> 8) & 0xFF
    data[fh + 3] = new_ls & 0xFF


def extract_jpeg_from_record(rec: bytes) -> bytes | None:
    """Pull a JPEG bitstream from one packed icon record (2+2+6 meta, then JPEG)."""
    if len(rec) < 12:
        return None
    soi = rec.find(b"\xff\xd8")
    if soi < 0:
        return None
    work = bytearray(rec[soi:])
    _repair_dwin_jpeg_sos_stuffing(work)
    end = work.rfind(b"\xff\xd9")
    if end < 0:
        return None
    return bytes(work[: end + 2])


@dataclass
class ICLParseResult:
    max_id: int
    version_byte: int
    file_size: int
    icons: list[tuple[int, int, int, bytes | None]]
    """(icon_id, width, height, jpeg_bytes_or_none) for each non-empty slot."""


def parse_icl(data: bytes) -> ICLParseResult:
    """
    Parse a DGUS_3-style .icl with dense index (icons 0..max_id, 4-byte BE offsets).
    Offsets are file-absolute. DwinTerminal pads the index to start payload at 32 KiB;
    Excel2ICL and other tools often pack tightly (first record immediately after index).
    """
    if len(data) < 16:
        raise ValueError("File too small to be a DGUS_3 ICL.")
    if data[0:6] != b"DGUS_3":
        raise ValueError("Not a DGUS_3 ICL (expected magic 'DGUS_3' at offset 0).")
    version_byte = data[12]
    max_id = int.from_bytes(data[13:15], "big")
    nslots = max_id + 1
    index_end = 16 + nslots * 4
    if index_end > len(data):
        raise ValueError(
            f"Index extends past EOF (need {index_end} bytes, file is {len(data)}). "
            "File may be truncated or not an ICL."
        )
    offsets: list[tuple[int, int]] = []
    for i in range(nslots):
        off = int.from_bytes(data[16 + i * 4 : 20 + i * 4], "big")
        offsets.append((i, off))
    active = [(i, o) for i, o in offsets if o != 0]
    active.sort(key=lambda x: x[1])
    icons: list[tuple[int, int, int, bytes | None]] = []
    for j, (icon_id, off) in enumerate(active):
        if off < index_end:
            raise ValueError(
                f"Icon {icon_id}: offset {off} overlaps index table "
                f"(index ends at {index_end}). File may be corrupt."
            )
        end = active[j + 1][1] if j + 1 < len(active) else len(data)
        if end <= off:
            continue
        rec = data[off:end]
        if len(rec) < 10:
            icons.append((icon_id, 0, 0, None))
            continue
        w = int.from_bytes(rec[0:2], "big")
        h = int.from_bytes(rec[2:4], "big")
        jpeg = extract_jpeg_from_record(rec)
        icons.append((icon_id, w, h, jpeg))
    return ICLParseResult(
        max_id=max_id,
        version_byte=version_byte,
        file_size=len(data),
        icons=icons,
    )


def resolve_icl_id(output_path: Path, icl_id: int | None) -> int:
    if icl_id is not None:
        return icl_id
    parsed = icl_numeric_id_from_filename(output_path.name)
    if parsed < 0:
        raise ValueError(
            "ICL library id: set explicitly or use an output name with a leading number (e.g. 48.icl)."
        )
    return parsed


def run_pack(
    output: Path,
    image_paths: list[Path],
    *,
    icl_id: int | None = None,
    quality: int = 90,
    align_head_kb: int = 32,
    lib_size_kb: int = 256,
    chroma: str = "411",
    keep_jpeg_raw: bool = False,
) -> tuple[bytes, int, int, int]:
    """
    Build .icl bytes from images.
    Returns (data, flash_slot_id, image_count, header_max_icon_id).
    chroma: DGUS sample type — 444 (4:4:4) or 411 (4:1:1, encoded as 4:2:0 in Pillow).
    keep_jpeg_raw: embed .jpg/.jpeg files without re-encoding (ignored for BMP/PNG).
    Raises ValueError / FileNotFoundError / RuntimeError on failure.
    """
    if not image_paths:
        raise ValueError("Select at least one image.")
    out = output
    slot_id = resolve_icl_id(out, icl_id)
    q = max(1, min(95, int(quality)))
    sub = chroma_to_subsampling(chroma) if not keep_jpeg_raw else None

    paths = [Path(p) for p in image_paths]
    for p in paths:
        if not p.is_file():
            raise FileNotFoundError(f"Not found: {p}")

    resolved = assign_icon_ids_from_paths(paths)
    icons: list[tuple[int, int, int, bytes, int]] = []
    for icon_id, p in resolved:
        w, h, jpg = load_image_jpeg_for_icl(
            p, q, sub, keep_jpeg_raw=keep_jpeg_raw
        )
        icons.append((icon_id, w, h, jpg, p.stat().st_size))

    data = pack_icl(
        icons,
        icl_slot_id=slot_id,
        align_head_kb=align_head_kb,
        lib_size_kb=lib_size_kb,
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    max_icon = max(i for i, _, _, _, _ in icons)
    return data, slot_id, len(icons), max_icon


def render_icl_preview_content(
    win: tk.Misc,
    host: ttk.Frame,
    path: Path,
    result: ICLParseResult,
) -> None:
    """
    Fill `host` with header + scrollable thumbnails. Double-click opens a larger window
    parented under `win`. PhotoImage refs are stored on host._icl_photo_refs for GC.
    """
    from PIL import Image, ImageTk

    for child in host.winfo_children():
        child.destroy()

    photo_refs: list[ImageTk.PhotoImage] = []
    setattr(host, "_icl_photo_refs", photo_refs)

    ok = sum(1 for _, _, _, j in result.icons if j)
    hdr = ttk.Frame(host, padding=8)
    hdr.pack(fill=tk.X)
    ttk.Label(
        hdr,
        text=(
            f"{path}  |  {result.file_size} bytes  |  header max_id={result.max_id}  "
            f"|  decoded JPEGs: {ok}/{len(result.icons)}"
        ),
        wraplength=860,
    ).pack(anchor="w")

    outer = ttk.Frame(host)
    outer.pack(fill=tk.BOTH, expand=True)
    canvas = tk.Canvas(outer, highlightthickness=0)
    vsb = ttk.Scrollbar(outer, orient=tk.VERTICAL, command=canvas.yview)
    canvas.configure(yscrollcommand=vsb.set)
    vsb.pack(side=tk.RIGHT, fill=tk.Y)
    canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    inner = ttk.Frame(canvas)
    canvas_win = canvas.create_window((0, 0), window=inner, anchor="nw")

    thumb_max = 200
    max_cols = 4
    row = col = 0

    def open_large(im: Image.Image, title: str) -> None:
        w2 = tk.Toplevel(win)
        w2.title(title)
        mw, mh = 900, 700
        disp = im.copy()
        disp.thumbnail((mw, mh), Image.Resampling.LANCZOS)
        ph = ImageTk.PhotoImage(disp)
        photo_refs.append(ph)
        lb = ttk.Label(w2, image=ph)
        lb.pack(padx=10, pady=10)

    for icon_id, w, h, jpeg in result.icons:
        cell = ttk.Frame(inner, relief=tk.GROOVE, padding=6)
        cell.grid(row=row, column=col, padx=6, pady=6, sticky="n")
        if jpeg:
            try:
                im = Image.open(io.BytesIO(jpeg)).convert("RGB")
                thumb = im.copy()
                thumb.thumbnail((thumb_max, thumb_max), Image.Resampling.LANCZOS)
                ph = ImageTk.PhotoImage(thumb)
                photo_refs.append(ph)
                img_lbl = ttk.Label(cell, image=ph)
                img_lbl.pack()
                img_lbl.bind(
                    "<Double-Button-1>",
                    lambda _e, img=im, tid=icon_id: open_large(
                        img, f"Icon {tid} ({w}×{h})"
                    ),
                )
            except Exception as ex:
                ttk.Label(cell, text=f"Decode error:\n{ex}", foreground="red").pack()
        else:
            ttk.Label(cell, text="(no JPEG)", foreground="#888").pack()
        ttk.Label(cell, text=f"id={icon_id}\nheader {w}×{h}").pack()
        col += 1
        if col >= max_cols:
            col = 0
            row += 1

    def _scroll(event: tk.Event) -> None:
        if event.delta:
            canvas.yview_scroll(int(-event.delta / 120), "units")

    canvas.bind("<MouseWheel>", _scroll)

    def _on_configure(_event: tk.Event) -> None:
        canvas.configure(scrollregion=canvas.bbox("all"))
        canvas.itemconfigure(canvas_win, width=canvas.winfo_width())

    inner.bind("<Configure>", _on_configure)
    canvas.bind("<Configure>", lambda e: canvas.itemconfigure(canvas_win, width=e.width))


def show_icl_preview(parent: tk.Misc, path: Path, result: ICLParseResult) -> None:
    """Popup window with ICL thumbnails (same content as Preview tab)."""
    top = tk.Toplevel(parent)
    top.title(f"ICL preview — {path.name}")
    top.minsize(640, 480)
    top.geometry("900x600")
    body = ttk.Frame(top, padding=6)
    body.pack(fill=tk.BOTH, expand=True)
    render_icl_preview_content(top, body, path, result)


def run_gui() -> None:
    """Tkinter front-end: tab (1) pack images → .icl, tab (2) open .icl and view icons."""
    root = tk.Tk()
    root.title("DGUS .icl — pack & preview")
    root.minsize(560, 420)
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)

    notebook = ttk.Notebook(root)
    notebook.grid(row=0, column=0, sticky="nsew", padx=6, pady=(6, 0))

    tab_pack = ttk.Frame(notebook, padding=10)
    notebook.add(tab_pack, text="Pack images → .icl")
    tab_pack.columnconfigure(0, weight=1)
    tab_pack.rowconfigure(1, weight=1)

    tab_preview = ttk.Frame(notebook, padding=10)
    notebook.add(tab_preview, text="Preview .icl")
    tab_preview.columnconfigure(0, weight=1)
    tab_preview.rowconfigure(1, weight=1)

    image_paths: list[str] = []

    frm = tab_pack

    ttk.Label(
        frm,
        text=(
            "Input images — icon id = leading digits in filename (32.png → 32); "
            "otherwise ids 0,1,2… in list order"
        ),
    ).grid(row=0, column=0, sticky="w")

    list_frame = ttk.Frame(frm)
    list_frame.grid(row=1, column=0, sticky="nsew", pady=(4, 8))
    list_frame.columnconfigure(0, weight=1)
    list_frame.rowconfigure(0, weight=1)

    listbox = tk.Listbox(list_frame, height=10, selectmode=tk.EXTENDED)
    listbox.grid(row=0, column=0, sticky="nsew")
    sb = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=listbox.yview)
    sb.grid(row=0, column=1, sticky="ns")
    listbox.configure(yscrollcommand=sb.set)

    btn_row = ttk.Frame(frm)
    btn_row.grid(row=2, column=0, sticky="ew", pady=(0, 8))

    def refresh_list() -> None:
        listbox.delete(0, tk.END)
        for p in image_paths:
            listbox.insert(tk.END, p)

    def add_files() -> None:
        paths = filedialog.askopenfilenames(
            title="Select images",
            filetypes=[
                ("Images", "*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff"),
                ("All files", "*.*"),
            ],
        )
        for p in paths:
            if p not in image_paths:
                image_paths.append(p)
        refresh_list()

    def remove_selected() -> None:
        sel = list(listbox.curselection())
        for i in reversed(sel):
            if 0 <= i < len(image_paths):
                del image_paths[i]
        refresh_list()

    def clear_all() -> None:
        image_paths.clear()
        refresh_list()

    ttk.Button(btn_row, text="Add files…", command=add_files).pack(side=tk.LEFT, padx=(0, 6))
    ttk.Button(btn_row, text="Remove selected", command=remove_selected).pack(side=tk.LEFT, padx=(0, 6))
    ttk.Button(btn_row, text="Clear", command=clear_all).pack(side=tk.LEFT)

    def open_icl_preview() -> None:
        p = filedialog.askopenfilename(
            title="Open ICL to preview",
            filetypes=[("ICL library", "*.icl"), ("All files", "*.*")],
        )
        if not p:
            return
        try:
            raw = Path(p).read_bytes()
            result = parse_icl(raw)
            show_icl_preview(root, Path(p), result)
            status.set(
                f"Popup preview: {len(result.icons)} icon(s) in {Path(p).name} — "
                "or use the Preview .icl tab"
            )
        except Exception as e:
            status.set(f"ICL preview error: {e}")
            messagebox.showerror("ICL preview", str(e))

    ttk.Button(btn_row, text="Open .icl in new window…", command=open_icl_preview).pack(
        side=tk.LEFT, padx=(16, 0)
    )

    # --- Preview .icl tab (same parser/thumbnails as popup) ---
    prev_top = ttk.Frame(tab_preview)
    prev_top.grid(row=0, column=0, sticky="ew", pady=(0, 8))
    prev_top.columnconfigure(1, weight=1)

    icl_preview_path = tk.StringVar()
    ttk.Label(prev_top, text="ICL file").grid(row=0, column=0, sticky="w", padx=(0, 8))
    ttk.Entry(prev_top, textvariable=icl_preview_path).grid(
        row=0, column=1, sticky="ew", padx=(0, 6)
    )

    def browse_icl_preview() -> None:
        p = filedialog.askopenfilename(
            title="Open ICL to preview",
            filetypes=[("ICL library", "*.icl"), ("All files", "*.*")],
        )
        if p:
            icl_preview_path.set(p)

    ttk.Button(prev_top, text="Browse…", command=browse_icl_preview).grid(row=0, column=2, padx=(0, 6))

    preview_host = ttk.Frame(tab_preview)
    preview_host.grid(row=1, column=0, sticky="nsew")

    def load_preview_tab() -> None:
        p = icl_preview_path.get().strip()
        if not p:
            messagebox.showwarning("Preview", "Choose an .icl file (Browse…).")
            return
        pp = Path(p)
        if not pp.is_file():
            messagebox.showerror("Preview", f"File not found:\n{p}")
            return
        try:
            result = parse_icl(pp.read_bytes())
            render_icl_preview_content(root, preview_host, pp, result)
            status.set(
                f"Preview tab: {len(result.icons)} icon(s) in {pp.name} — double-click to enlarge"
            )
        except Exception as e:
            status.set(f"Preview error: {e}")
            messagebox.showerror("ICL preview", str(e))

    ttk.Button(prev_top, text="Load & show icons", command=load_preview_tab).grid(
        row=0, column=3, sticky="e"
    )

    out_row = ttk.Frame(frm)
    out_row.grid(row=3, column=0, sticky="ew", pady=(0, 6))
    out_row.columnconfigure(1, weight=1)

    ttk.Label(out_row, text="Output .icl").grid(row=0, column=0, sticky="w", padx=(0, 8))
    out_var = tk.StringVar()
    out_entry = ttk.Entry(out_row, textvariable=out_var)
    out_entry.grid(row=0, column=1, sticky="ew", padx=(0, 6))

    def browse_out() -> None:
        p = filedialog.asksaveasfilename(
            title="Save .icl as",
            defaultextension=".icl",
            filetypes=[("ICL library", "*.icl"), ("All files", "*.*")],
        )
        if p:
            out_var.set(p)

    ttk.Button(out_row, text="Browse…", command=browse_out).grid(row=0, column=2)

    def _sync_flash_slot_from_output(*_a: object) -> None:
        if icl_var.get().strip():
            return
        s = out_var.get().strip()
        if not s:
            return
        pid = icl_numeric_id_from_filename(Path(s).name)
        if pid >= 0:
            icl_var.set(str(pid))

    out_var.trace_add("write", _sync_flash_slot_from_output)

    opt_row = ttk.Frame(frm)
    opt_row.grid(row=4, column=0, sticky="ew", pady=(0, 6))

    ttk.Label(opt_row, text="Flash slot id (optional)").grid(
        row=0, column=0, sticky="w", padx=(0, 8)
    )
    icl_var = tk.StringVar()
    ttk.Entry(opt_row, textvariable=icl_var, width=8).grid(row=0, column=1, sticky="w", padx=(0, 16))
    ttk.Label(opt_row, text="JPEG quality").grid(row=0, column=2, sticky="w", padx=(0, 8))
    qual_var = tk.IntVar(value=90)
    ttk.Spinbox(opt_row, from_=1, to=95, textvariable=qual_var, width=5).grid(
        row=0, column=3, sticky="w"
    )

    adv_row = ttk.Frame(frm)
    adv_row.grid(row=5, column=0, sticky="ew", pady=(0, 6))

    ttk.Label(adv_row, text="Align head (KB, 0=tight)").grid(
        row=0, column=0, sticky="w", padx=(0, 8)
    )
    align_var = tk.IntVar(value=32)
    ttk.Spinbox(adv_row, from_=0, to=512, textvariable=align_var, width=5).grid(
        row=0, column=1, sticky="w", padx=(0, 16)
    )
    ttk.Label(adv_row, text="Lib size").grid(row=0, column=2, sticky="w", padx=(0, 8))
    lib_var = tk.StringVar(value="256")
    ttk.Combobox(
        adv_row,
        textvariable=lib_var,
        values=("256", "512"),
        width=5,
        state="readonly",
    ).grid(row=0, column=3, sticky="w", padx=(0, 16))
    ttk.Label(adv_row, text="Chroma").grid(row=0, column=4, sticky="w", padx=(0, 8))
    chroma_var = tk.StringVar(value="411")
    ttk.Combobox(
        adv_row,
        textvariable=chroma_var,
        values=("411", "444"),
        width=5,
        state="readonly",
    ).grid(row=0, column=5, sticky="w")

    keep_jpeg_var = tk.BooleanVar(value=False)
    ttk.Checkbutton(
        adv_row,
        text="Keep .jpg/.jpeg as-is (no re-encode; matches DGUS if file is their JPEG)",
        variable=keep_jpeg_var,
    ).grid(row=1, column=0, columnspan=6, sticky="w", pady=(6, 0))

    status = tk.StringVar(value="Ready. Use tabs: Pack images or Preview .icl.")
    status_bar = ttk.Frame(root, padding=(8, 4))
    status_bar.grid(row=1, column=0, sticky="ew")
    ttk.Label(status_bar, textvariable=status, foreground="#333").pack(anchor="w")

    def do_generate() -> None:
        out_s = out_var.get().strip()
        if not image_paths:
            messagebox.showwarning("Images", "Add at least one image.")
            return
        if not out_s:
            messagebox.showwarning("Output", "Choose an output .icl path.")
            return
        out_path = Path(out_s)
        icl_raw = icl_var.get().strip()
        icl_id: int | None
        if icl_raw == "":
            icl_id = None
        else:
            try:
                icl_id = int(icl_raw)
            except ValueError:
                messagebox.showerror("ICL id", "ICL id must be empty or an integer (e.g. 48).")
                return
            if icl_id < 0:
                messagebox.showerror("ICL id", "ICL id must be non-negative.")
                return

        try:
            lib_kb = int(lib_var.get())
            if lib_kb not in (256, 512):
                messagebox.showerror("Lib size", "Lib size must be 256 or 512 (KiB).")
                return
            data, slot_id, n, max_icon = run_pack(
                out_path,
                [Path(p) for p in image_paths],
                icl_id=icl_id,
                quality=int(qual_var.get()),
                align_head_kb=max(0, int(align_var.get())),
                lib_size_kb=lib_kb,
                chroma=chroma_var.get().strip(),
                keep_jpeg_raw=keep_jpeg_var.get(),
            )
            msg = (
                f"Wrote {out_path}\n{len(data)} bytes, flash_slot={slot_id}, "
                f"header max icon id={max_icon}, files={n}"
            )
            status.set(msg.replace("\n", " — "))
            messagebox.showinfo("Done", msg)
        except Exception as e:
            status.set(f"Error: {e}")
            messagebox.showerror("Error", str(e))

    ttk.Button(frm, text="Generate .icl", command=do_generate).grid(
        row=7, column=0, sticky="e", pady=(12, 0)
    )

    root.mainloop()


def main() -> int:
    ap = argparse.ArgumentParser(description="Pack images into DGUS-style .icl (DGUS_3 + JPEG)")
    ap.add_argument(
        "--gui",
        "-g",
        action="store_true",
        help="Open file picker GUI instead of CLI",
    )
    ap.add_argument("-o", "--output", type=Path, help="Output .icl path (e.g. 48.icl)")
    ap.add_argument(
        "--icl-id",
        type=int,
        default=None,
        help="Flash slot id (default: leading digits of output stem, e.g. 48.icl -> 48)",
    )
    ap.add_argument("--quality", type=int, default=90, help="JPEG quality 1-95 (default 90)")
    ap.add_argument(
        "--align-head-kb",
        type=int,
        default=32,
        metavar="N",
        help="Align header+index: pad start of JPEG area to N KiB (0=tight pack after index)",
    )
    ap.add_argument(
        "--lib-size-kb",
        type=int,
        choices=(256, 512),
        default=256,
        help="ICL library flash stride for bank padding (256 or 512 KiB, default 256)",
    )
    ap.add_argument(
        "--chroma",
        type=str,
        default="411",
        metavar="411|444",
        help="JPEG chroma: 411 (stronger subsampling) or 444 (4:4:4)",
    )
    ap.add_argument(
        "--keep-jpeg",
        action="store_true",
        help="For .jpg/.jpeg inputs only: embed file bytes without Pillow re-encode",
    )
    ap.add_argument(
        "--preview",
        type=Path,
        metavar="FILE.icl",
        help="Parse an ICL and print icon summary (no images required)",
    )
    ap.add_argument("images", nargs="*", type=Path, help="Input images (PNG, JPEG, etc.)")
    args = ap.parse_args()

    if args.preview:
        try:
            r = parse_icl(args.preview.read_bytes())
        except Exception as e:
            print(e, file=sys.stderr)
            return 1
        print(f"{args.preview}: {r.file_size} bytes, max_id={r.max_id}, slots={len(r.icons)}")
        for i, w, h, j in r.icons:
            je = len(j) if j else 0
            print(f"  id={i}  {w}x{h}  jpeg={je} bytes")
        return 0

    if args.gui:
        run_gui()
        return 0

    if not args.output or not args.images:
        ap.error("CLI requires --output and at least one image, or use --gui / run with no arguments")

    out = args.output
    try:
        _, slot_id, n, max_icon = run_pack(
            out,
            list(args.images),
            icl_id=args.icl_id,
            quality=args.quality,
            align_head_kb=max(0, args.align_head_kb),
            lib_size_kb=args.lib_size_kb,
            chroma=args.chroma,
            keep_jpeg_raw=args.keep_jpeg,
        )
    except Exception as e:
        print(e, file=sys.stderr)
        return 1
    print(f"Wrote {out}, flash_slot={slot_id}, header_max_icon_id={max_icon}, images={n}")
    return 0


if __name__ == "__main__":
    try:
        if len(sys.argv) == 1:
            run_gui()
        else:
            raise SystemExit(main())
    except ImportError as e:
        if "PIL" in str(e):
            print("Install Pillow: pip install Pillow", file=sys.stderr)
        raise
