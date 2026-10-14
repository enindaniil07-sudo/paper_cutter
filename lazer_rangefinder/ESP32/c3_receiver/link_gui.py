"""
График миллиметров, которые C3 принял по радио от S3.

К компьютеру подключаются S3 и C3. Nano в этой цепочке нет.
Строка с C3: "принято <номер>  <миллиметры> мм"

Запуск:
  python link_gui.py
  python link_gui.py COM11
"""

import importlib.util
import re
import sys
import time
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
src = ROOT / "Due" / "tof200c_monitor" / "distance_gui.py"
spec = importlib.util.spec_from_file_location("distance_gui", src)
gui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gui)

LINE_MM = re.compile(r"принято\s+\d+\s+(\d+)\s*мм")


def esp_ports():
    found = []
    try:
        usb = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Enum\USB")
    except OSError:
        return found
    index = 0
    while True:
        try:
            vidpid = winreg.EnumKey(usb, index)
        except OSError:
            break
        index += 1
        if not vidpid.upper().startswith("VID_303A"):
            continue
        try:
            device = winreg.OpenKey(usb, vidpid)
        except OSError:
            continue
        inst = 0
        while True:
            try:
                name = winreg.EnumKey(device, inst)
            except OSError:
                break
            inst += 1
            try:
                params = winreg.OpenKey(device, name + r"\Device Parameters")
                port, _typ = winreg.QueryValueEx(params, "PortName")
                winreg.CloseKey(params)
            except OSError:
                continue
            port = str(port)
            if port.upper().startswith("COM"):
                found.append(port)
        winreg.CloseKey(device)
    winreg.CloseKey(usb)
    active = set(gui.list_ports())
    return sorted({port for port in found if port in active}, key=lambda port: (len(port), port))


def sniff(name, seconds=1.2):
    port = gui.SerialPort()
    buf = bytearray()
    try:
        port.open(name, gui.BAUD)
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            buf.extend(port.read())
    except OSError:
        return ""
    finally:
        port.close()
    return buf.decode("utf-8", "replace")


def find_c3_port():
    ports = esp_ports()
    unknown = []
    for name in ports:
        text = sniff(name)
        if "принято" in text or "C3 принимает" in text or "слушаю" in text:
            return name
        if "ушло" in text or "S3-Zero" in text:
            continue
        unknown.append(name)
    if len(unknown) == 1:
        return unknown[0]
    if "COM11" in ports:
        return "COM11"
    if ports:
        return ports[-1]
    return "COM11"


class App(gui.App):
    def __init__(self, port_name=None):
        super().__init__(port_name or find_c3_port())
        self.title("Датчик → S3 → C3")

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
                match = LINE_MM.search(text)
                if match:
                    self.events.put((
                        "sample",
                        {
                            "mm": int(match.group(1)),
                            "ok": True,
                            "mean": None,
                            "sd": None,
                            "t": time.monotonic(),
                        },
                    ))
                elif "нет замера" in text:
                    self.events.put(("status", "радио есть, кадра от датчика ещё нет"))
                elif "пакетов нет" in text:
                    self.events.put(("status", "пакетов от S3 нет"))
                elif text.startswith("слушаю"):
                    self.events.put(("status", "C3 слушает канал 1"))
                elif "C3 принимает" in text:
                    self.events.put(("status", "C3 слушает канал 1"))
        self.events.put(("closed", None))

    def pump(self):
        changed = False
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except gui.queue.Empty:
                break
            if kind == "sample":
                self.samples.append(payload)
                self.last = payload
                changed = True
            elif kind == "status" and self.running:
                port_name = self.port_var.get().strip()
                self.status.configure(text=f"{port_name}  {payload}", fg=gui.LINE)
            elif kind == "error":
                self.status.configure(text=payload, fg=gui.BAD)
                self.stop()
            elif kind == "closed" and not self.running:
                pass
        if self.last is not None and changed:
            sample = self.last
            color = gui.INK if sample["mm"] < 10000 else gui.BAD
            self.value.configure(text=f"{sample['mm']} мм", fg=color)
        now = time.monotonic()
        if changed and now >= self._next_draw:
            self._next_draw = now + gui.DRAW_EVERY_S
            self.show_value()
            self.draw()
        self.after(16, self.pump)


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else find_c3_port()
    App(name).mainloop()
