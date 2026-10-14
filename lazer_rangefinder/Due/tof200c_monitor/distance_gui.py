"""
Окно дистанции TOF200C.

Читает Arduino Due (115200) и рисует миллиметры и график.
Нужен только Python со стандартной библиотекой, Windows.

Запуск:
  python distance_gui.py
"""

import ctypes
import queue
import re
import statistics
import sys
import threading
import time
import tkinter as tk
from collections import deque
from ctypes import wintypes
import winreg

BAUD = 115200
DEFAULT_PORT = "COM6"
HISTORY = 400
DRAW_EVERY_S = 0.1

LINE_MM = re.compile(r"^(--)?\s*(-?\d+)\s*мм\b")
LINE_MEAN = re.compile(r"средн\s+(-?\d+(?:[.,]\d+)?)")
LINE_SD = re.compile(r"СКО\s+(\d+(?:[.,]\d+)?)")

BG = "#16191e"
PANEL = "#1e232b"
INK = "#f4f7fb"
MUTED = "#9aa3b2"
LINE = "#3ddc97"
BAD = "#ff6b6b"
GRID = "#2c3340"

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class DCB(ctypes.Structure):
    _fields_ = [
        ("DCBlength", wintypes.DWORD),
        ("BaudRate", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("wReserved", wintypes.WORD),
        ("XonLim", wintypes.WORD),
        ("XoffLim", wintypes.WORD),
        ("ByteSize", wintypes.BYTE),
        ("Parity", wintypes.BYTE),
        ("StopBits", wintypes.BYTE),
        ("XonChar", ctypes.c_char),
        ("XoffChar", ctypes.c_char),
        ("ErrorChar", ctypes.c_char),
        ("EofChar", ctypes.c_char),
        ("EvtChar", ctypes.c_char),
        ("wReserved1", wintypes.WORD),
    ]


class COMMTIMEOUTS(ctypes.Structure):
    _fields_ = [
        ("ReadIntervalTimeout", wintypes.DWORD),
        ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
        ("ReadTotalTimeoutConstant", wintypes.DWORD),
        ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
        ("WriteTotalTimeoutConstant", wintypes.DWORD),
    ]


kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.GetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(DCB)]
kernel32.GetCommState.restype = wintypes.BOOL
kernel32.SetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(DCB)]
kernel32.SetCommState.restype = wintypes.BOOL
kernel32.SetCommTimeouts.argtypes = [wintypes.HANDLE, ctypes.POINTER(COMMTIMEOUTS)]
kernel32.SetCommTimeouts.restype = wintypes.BOOL
kernel32.EscapeCommFunction.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.EscapeCommFunction.restype = wintypes.BOOL
kernel32.PurgeComm.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.PurgeComm.restype = wintypes.BOOL

INVALID_HANDLE = wintypes.HANDLE(-1).value
CLRDTR = 6
CLRRTS = 4


def list_ports():
    found = []
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
    except OSError:
        return found
    index = 0
    while True:
        try:
            _name, value, _typ = winreg.EnumValue(key, index)
        except OSError:
            break
        found.append(str(value))
        index += 1
    winreg.CloseKey(key)
    return sorted(set(found), key=lambda name: (len(name), name))


def parse_line(text):
    match = LINE_MM.match(text.strip())
    if match is None:
        return None
    sample = {
        "mm": int(match.group(2)),
        "ok": match.group(1) is None,
        "mean": None,
        "sd": None,
    }
    mean = LINE_MEAN.search(text)
    sd = LINE_SD.search(text)
    if mean is not None:
        sample["mean"] = float(mean.group(1).replace(",", "."))
    if sd is not None:
        sample["sd"] = float(sd.group(1).replace(",", "."))
    return sample


class SerialPort:
    def __init__(self):
        self.handle = None

    def open(self, name, baud):
        self.close()
        handle = kernel32.CreateFileW(
            "\\\\.\\" + name,
            0x80000000 | 0x40000000,
            0,
            None,
            3,
            0x80,
            None,
        )
        if handle == INVALID_HANDLE:
            raise ctypes.WinError(ctypes.get_last_error())
        self.handle = handle
        kernel32.EscapeCommFunction(handle, CLRDTR)
        kernel32.EscapeCommFunction(handle, CLRRTS)
        dcb = DCB()
        dcb.DCBlength = ctypes.sizeof(DCB)
        if not kernel32.GetCommState(handle, ctypes.byref(dcb)):
            err = ctypes.WinError(ctypes.get_last_error())
            self.close()
            raise err
        dcb.BaudRate = baud
        dcb.ByteSize = 8
        dcb.Parity = 0
        dcb.StopBits = 0
        dcb.flags = 1
        if not kernel32.SetCommState(handle, ctypes.byref(dcb)):
            err = ctypes.WinError(ctypes.get_last_error())
            self.close()
            raise err
        kernel32.EscapeCommFunction(handle, CLRDTR)
        kernel32.EscapeCommFunction(handle, CLRRTS)
        timeouts = COMMTIMEOUTS(50, 0, 80, 0, 0)
        kernel32.SetCommTimeouts(handle, ctypes.byref(timeouts))
        kernel32.PurgeComm(handle, 0x0008 | 0x0004)

    def read(self, size=512):
        if self.handle is None:
            return b""
        buf = ctypes.create_string_buffer(size)
        got = wintypes.DWORD(0)
        ok = kernel32.ReadFile(self.handle, buf, size, ctypes.byref(got), None)
        if not ok:
            err = ctypes.get_last_error()
            if err in (995, 996):
                return b""
            raise ctypes.WinError(err)
        return buf.raw[: got.value]

    def close(self):
        if self.handle not in (None, INVALID_HANDLE):
            kernel32.CloseHandle(self.handle)
        self.handle = None


class App(tk.Tk):
    def __init__(self, port_name=DEFAULT_PORT):
        super().__init__()
        self.title("GL-R01 — дистанция")
        self.geometry("980x640")
        self.minsize(760, 480)
        self.configure(bg=BG)
        self.port = SerialPort()
        self.events = queue.Queue()
        self.worker = None
        self.running = False
        self.samples = deque(maxlen=HISTORY)
        self.last = None
        self._next_draw = 0.0

        top = tk.Frame(self, bg=BG)
        top.pack(fill="x", padx=16, pady=(14, 0))

        self.port_var = tk.StringVar(value=port_name)
        ports = list_ports()
        if port_name not in ports:
            ports.insert(0, port_name)
        self.port_box = tk.OptionMenu(top, self.port_var, *ports)
        self.port_box.configure(
            bg=PANEL, fg=INK, activebackground=PANEL, activeforeground=INK,
            highlightthickness=0, relief="flat",
        )
        self.port_box.pack(side="left")

        self.connect_btn = tk.Button(
            top, text="Подключить", command=self.toggle,
            bg=LINE, fg="#102117", activebackground="#62e8b0",
            relief="flat", padx=14, pady=6,
        )
        self.connect_btn.pack(side="left", padx=(10, 0))

        tk.Button(
            top, text="Очистить", command=self.clear_plot,
            bg=PANEL, fg=INK, activebackground="#2a3140", activeforeground=INK,
            relief="flat", padx=14, pady=6,
        ).pack(side="left", padx=(8, 0))

        self.status = tk.Label(top, text="не подключено", bg=BG, fg=MUTED, anchor="e")
        self.status.pack(side="right", fill="x", expand=True)

        read = tk.Frame(self, bg=BG)
        read.pack(fill="x", padx=16, pady=(8, 0))
        self.value = tk.Label(
            read, text="— мм", bg=BG, fg=INK,
            font=("Segoe UI", 54, "bold"), anchor="w",
        )
        self.value.pack(side="left")
        stats = tk.Frame(read, bg=BG)
        stats.pack(side="left", padx=(24, 0), pady=(8, 0))
        self.mean_label = tk.Label(stats, text="среднее  —", bg=BG, fg=MUTED, font=("Segoe UI", 14), anchor="w")
        self.mean_label.pack(anchor="w")
        self.sd_label = tk.Label(stats, text="СКО  —", bg=BG, fg=MUTED, font=("Segoe UI", 14), anchor="w")
        self.sd_label.pack(anchor="w")
        self.count_label = tk.Label(stats, text="точек  0", bg=BG, fg=MUTED, font=("Segoe UI", 14), anchor="w")
        self.count_label.pack(anchor="w")

        self.canvas = tk.Canvas(self, bg=PANEL, highlightthickness=0, height=380)
        self.canvas.pack(fill="both", expand=True, padx=16, pady=16)
        self.canvas.bind("<Configure>", lambda _event: self.draw())

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(16, self.pump)
        self.after(200, self.toggle)

    def refresh_ports(self):
        ports = list_ports() or [DEFAULT_PORT]
        menu = self.port_box["menu"]
        menu.delete(0, "end")
        current = self.port_var.get()
        if current not in ports:
            ports.insert(0, current)
        for name in ports:
            menu.add_command(label=name, command=lambda value=name: self.port_var.set(value))

    def toggle(self):
        if self.running:
            self.stop()
        else:
            self.start()

    def start(self):
        self.refresh_ports()
        name = self.port_var.get().strip()
        try:
            self.port.open(name, BAUD)
        except OSError as err:
            self.status.configure(text=f"{name} не открылся: {err.winerror if hasattr(err, 'winerror') else err}", fg=BAD)
            return
        self.running = True
        self.connect_btn.configure(text="Отключить", bg=BAD, fg=INK, activebackground="#ff8d8d")
        self.status.configure(text=f"{name}  {BAUD}", fg=LINE)
        self.worker = threading.Thread(target=self.read_loop, daemon=True)
        self.worker.start()

    def stop(self):
        self.running = False
        self.port.close()
        self.connect_btn.configure(text="Подключить", bg=LINE, fg="#102117", activebackground="#62e8b0")
        self.status.configure(text="не подключено", fg=MUTED)

    def read_loop(self):
        buf = bytearray()
        while self.running:
            try:
                chunk = self.port.read()
            except OSError as err:
                self.events.put(("error", str(err)))
                break
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, buf = buf.partition(b"\n")
                text = raw.decode("utf-8", "replace").strip("\r")
                sample = parse_line(text)
                if sample is not None:
                    sample["t"] = time.monotonic()
                    self.events.put(("sample", sample))
        self.events.put(("closed", None))

    def pump(self):
        changed = False
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "sample":
                self.samples.append(payload)
                self.last = payload
                changed = True
            elif kind == "error":
                self.status.configure(text=payload, fg=BAD)
                self.stop()
            elif kind == "closed" and not self.running:
                pass
        if self.last is not None and changed:
            sample = self.last
            color = INK if sample["mm"] < 10000 else BAD
            self.value.configure(text=f"{sample['mm']} мм", fg=color)
        now = time.monotonic()
        if changed and now >= self._next_draw:
            self._next_draw = now + DRAW_EVERY_S
            self.show_value()
            self.draw()
        self.after(16, self.pump)

    def show_value(self):
        sample = self.last
        if sample is None:
            return
        raw_items = [item for item in self.samples if 0 <= item["mm"] < 10000]
        color = INK if sample["mm"] < 10000 else BAD
        self.value.configure(text=f"{sample['mm']} мм", fg=color)
        raw = [item["mm"] for item in raw_items]
        if raw:
            mean_text = f"среднее  {statistics.fmean(raw):.1f} мм"
        else:
            mean_text = "среднее  —"
        if len(raw) >= 2:
            sd_text = f"СКО  {statistics.pstdev(raw):.1f} мм"
        else:
            sd_text = "СКО  —"
        self.mean_label.configure(text=mean_text)
        self.sd_label.configure(text=sd_text)
        self.count_label.configure(text=f"сырых  {len(raw)}")

    def clear_plot(self):
        self.samples.clear()
        self.last = None
        self.value.configure(text="— мм", fg=INK)
        self.mean_label.configure(text="среднее  —")
        self.sd_label.configure(text="СКО  —")
        self.count_label.configure(text="сырых  0")
        self.draw()

    def draw(self):
        canvas = self.canvas
        canvas.delete("all")
        width = max(canvas.winfo_width(), 10)
        height = max(canvas.winfo_height(), 10)
        left, right, top, bottom = 58, 16, 16, 28
        plot_w = max(width - left - right, 1)
        plot_h = max(height - top - bottom, 1)
        canvas.create_rectangle(left, top, left + plot_w, top + plot_h, fill="#191d24", outline="")

        good = [item for item in self.samples if 0 <= item["mm"] < 10000]
        if good:
            t_last = good[-1]["t"]
            good = [item for item in good if item["t"] >= t_last - 8.0]
        if len(good) < 2:
            canvas.create_text(
                left + plot_w / 2, top + plot_h / 2,
                text="жду замеры с датчика", fill=MUTED, font=("Segoe UI", 16),
            )
            return

        values = [item["mm"] for item in good]
        lo = min(values)
        hi = max(values)
        span = hi - lo
        pad = max(5.0, span * 0.18)
        y0 = lo - pad
        y1 = hi + pad
        scale = plot_h / (y1 - y0)

        def y_of(mm):
            return top + plot_h - (mm - y0) * scale

        for step in range(5):
            yy = top + plot_h * step / 4
            mm = y1 - (y1 - y0) * step / 4
            canvas.create_line(left, yy, left + plot_w, yy, fill=GRID)
            canvas.create_text(left - 8, yy, text=f"{mm:.0f}", fill=MUTED, anchor="e", font=("Segoe UI", 9))

        t0 = good[0]["t"]
        t1 = good[-1]["t"]
        span_t = max(t1 - t0, 1e-3)

        def x_of(stamp):
            return left + (stamp - t0) / span_t * plot_w

        points = []
        step = max(1, len(good) // 160)
        plotted = list(good)[::step]
        if plotted[-1] is not good[-1]:
            plotted.append(good[-1])
        for item in plotted:
            points.extend((x_of(item["t"]), y_of(item["mm"])))
        canvas.create_line(*points, fill=LINE, width=2, smooth=False)
        lx, ly = points[-2], points[-1]
        canvas.create_oval(lx - 4, ly - 4, lx + 4, ly + 4, fill=LINE, outline="")

        seconds = t1 - t0
        canvas.create_text(left, height - 8, text="0 с", fill=MUTED, anchor="w", font=("Segoe UI", 9))
        canvas.create_text(left + plot_w, height - 8, text=f"{seconds:.0f} с", fill=MUTED, anchor="e", font=("Segoe UI", 9))

    def on_close(self):
        self.running = False
        self.port.close()
        self.destroy()


def once(port_name):
    port = SerialPort()
    port.open(port_name, BAUD)
    buf = bytearray()
    deadline = time.monotonic() + 4
    found = 0
    try:
        while time.monotonic() < deadline and found < 3:
            buf.extend(port.read())
            while b"\n" in buf:
                raw, _, buf = buf.partition(b"\n")
                sample = parse_line(raw.decode("utf-8", "replace"))
                if sample is not None and sample["ok"]:
                    print(sample["mm"])
                    found += 1
    finally:
        port.close()
    return found


if __name__ == "__main__":
    args = [item for item in sys.argv[1:] if item != "--once"]
    name = args[0] if args else DEFAULT_PORT
    if "--once" in sys.argv:
        raise SystemExit(0 if once(name) else 1)
    App(name).mainloop()
