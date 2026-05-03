"""Threaded serial I/O to the Arduino (newline-delimited lines)."""

from __future__ import annotations

import threading
import time
from typing import Callable

import serial


class SerialBridge:
    def __init__(
        self,
        port: str,
        baud: int = 9600,
        on_line: Callable[[str], None] | None = None,
    ):
        self.port = port
        self.baud = baud
        self.on_line = on_line
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()
        self._reader_thread: threading.Thread | None = None
        self._running = threading.Event()

    def open(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
        time.sleep(0.1)
        self._running.set()
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

    def close(self) -> None:
        self._running.clear()
        if self._ser:
            self._ser.close()
            self._ser = None

    def send_line(self, line: str) -> None:
        if self._ser is None:
            return
        data = line if line.endswith("\n") else line + "\n"
        with self._lock:
            self._ser.write(data.encode("utf-8", errors="replace"))
            self._ser.flush()

    def _read_loop(self) -> None:
        buf = bytearray()
        assert self._ser is not None
        while self._running.is_set():
            try:
                chunk = self._ser.read(256)
                if not chunk:
                    continue
                buf.extend(chunk)
                while b"\n" in buf:
                    idx = buf.index(b"\n")
                    raw = buf[: idx + 1]
                    del buf[: idx + 1]
                    try:
                        text = raw.decode("utf-8", errors="replace").strip()
                    except Exception:
                        continue
                    if text and self.on_line:
                        self.on_line(text)
            except Exception:
                time.sleep(0.05)
