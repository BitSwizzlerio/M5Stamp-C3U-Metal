"""
console.py - the board's Lua console, over its USB serial port.

The same rules as tools/board.py and tools/monitor.py in the project: find the
board by its USB IDs (Espressif 0x303A, USB Serial/JTAG 0x1001), open the port
with DTR and RTS off so that opening it doesn't reset the board, and come back
by itself when the board is unplugged or resets.
"""
import os
import threading
import time

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
USB_SERIAL_JTAG_PID = 0x1001


def find_port():
    for port in sorted(list_ports.comports(), key=lambda p: p.device):
        if port.vid == ESPRESSIF_VID and port.pid == USB_SERIAL_JTAG_PID:
            return port.device
    return None


class Console:
    def __init__(self, on_data, on_status, port=None):
        self.on_data = on_data              # called with bytes from the board
        self.on_status = on_status          # called when the connection changes
        self.fixed_port = port
        self.link = None
        self.port = None
        self.paused = False
        self.problem = ""                   # why the port can't be opened, if it can't
        self.tail = b""                     # the last few bytes, to spot a prompt
        self.write_lock = threading.Lock()
        self.running = True
        threading.Thread(target=self._run, daemon=True).start()

    @property
    def connected(self):
        return self.link is not None

    def _open(self, port):
        """Open the port without resetting the board.

        The board's USB port treats DTR and RTS like the reset and boot-mode wires of
        older ESP32 boards: DTR off with RTS on holds the chip in reset. On Windows the
        lines can be set before the port opens, so both simply stay off. Linux switches
        both on as the port opens, and pyserial would then switch DTR off before RTS,
        passing through that reset state; so here RTS goes off first, then DTR."""
        link = serial.Serial()
        link.port = port
        link.baudrate = 115200              # USB ignores it, but it must be set
        link.timeout = 0.05
        if os.name == "nt":
            link.dtr = False
            link.rts = False
            link.open()                     # Windows lets only one program open a port
        else:
            link.exclusive = True           # Linux would let any number share it, each taking some of the text
            link.open()                     # both lines on, as Linux left them: not a reset
            link.rts = False                # DTR on, RTS off: not a reset either
            link.dtr = False                # both off
        return link

    def _run(self):
        while self.running:
            if self.paused or self.link is None:
                if not self.paused:
                    port = self.fixed_port or find_port()
                    if port:
                        try:
                            self.link, self.port = self._open(port), port
                            self.problem = ""
                            self.on_status()
                        except (serial.SerialException, OSError) as error:
                            self.link = None
                            text = str(error).lower()
                            # On Windows "access is denied" means another program has the port open.
                            if os.name == "nt":
                                problem = "busy" if "denied" in text or "permission" in text else ""
                            else:
                                problem = ("permission" if "permission" in text
                                           else "busy" if "busy" in text or "exclusive" in text
                                           or "temporarily unavailable" in text else "")
                            if problem != self.problem:
                                self.problem = problem
                                self.on_status()
                if self.link is None:
                    time.sleep(0.3)
                    continue
            try:
                data = self.link.read(512)
            except (serial.SerialException, OSError, TypeError, AttributeError):
                self._drop()
                time.sleep(0.5)             # don't reopen straight away: a fault mustn't become a fast loop
                continue
            if data:
                self.tail = (self.tail + data)[-8:]
                self.on_data(data)

    def _drop(self):
        link, self.link = self.link, None
        if link:
            try:
                link.close()
            except Exception:
                pass
        self.on_status()

    def write(self, data):
        with self.write_lock:
            if self.link is None:
                return False
            try:
                self.link.write(data)
                return True
            except (serial.SerialException, OSError):
                self._drop()
                return False

    def pause(self):
        """Let go of the port, for flashing. resume() opens it again."""
        self.paused = True
        self._drop()

    def resume(self):
        self.paused = False

    def close(self):
        self.running = False
        self._drop()

    # ---- typing a program in, line by line ------------------------------------------
    def at_prompt(self):
        """The board is waiting for a line: its output ends with "> " or ">> "."""
        return self.tail.endswith(b"> ")

    def wait_for_prompt(self, timeout, stop):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not stop.is_set():
            if self.at_prompt():
                return True
            time.sleep(0.01)
        return False

    def type_lines(self, text, stop, report):
        """Type each line of a Lua program when the board is ready for it, like a person would.

        Multi-line statements work: the board shows ">> " while one is unfinished.
        Blank lines are skipped. Returns True if every line went in."""
        lines = [l for l in text.replace("\r\n", "\n").replace("\r", "\n").split("\n") if l.strip()]
        if not self.at_prompt():
            self.write(b"\r")                       # the board only prints a prompt once asked
        for number, line in enumerate(lines, start=1):
            if not self.wait_for_prompt(60 if number > 1 else 5, stop):
                if stop.is_set():
                    report(f"stopped at line {number} of {len(lines)}")
                else:
                    report(f"line {number - 1} is still running after 60 s, so the rest wasn't typed. "
                           "Press Stop to interrupt it.")
                return False
            self.tail = b""
            self.write(line.encode("utf-8") + b"\r")
        report(f"typed {len(lines)} lines")
        return True
