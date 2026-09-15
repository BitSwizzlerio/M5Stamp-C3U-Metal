#!/usr/bin/env python3
"""
monitor.py - a terminal for the board's Lua console.

    python tools/monitor.py [--port PORT]

Everything you type goes to the board, including Ctrl-C, which stops a
running Lua program. Press Ctrl-] to quit. If the board is unplugged or
resets, the monitor waits for it to come back.

It also takes input from a pipe, which is handy in scripts:

    echo print(1 + 2) | python tools/monitor.py

This types each line once the board shows a prompt, prints the replies, and
quits when the last line has finished.
"""
import argparse
import os
import sys
import threading
import time

import board
import serial

QUIT_KEY = b"\x1d"          # Ctrl-]


class Keyboard:
    """Read one key at a time, without echo, with Ctrl-C arriving as a key instead of stopping Python."""

    def __enter__(self):
        if os.name == "nt":
            import ctypes
            import msvcrt
            self.msvcrt = msvcrt
            self.kernel32 = ctypes.windll.kernel32
            self.handle = self.kernel32.GetStdHandle(-10)                 # the console's input
            self.old_mode = ctypes.c_uint32()
            self.kernel32.GetConsoleMode(self.handle, ctypes.byref(self.old_mode))
            self.kernel32.SetConsoleMode(self.handle, self.old_mode.value & ~0x0001)  # ENABLE_PROCESSED_INPUT off
        else:
            import termios
            self.termios = termios
            self.fd = sys.stdin.fileno()
            self.old_mode = termios.tcgetattr(self.fd)
            mode = termios.tcgetattr(self.fd)
            mode[0] &= ~(termios.ICRNL | termios.IXON)                    # Enter stays \r; Ctrl-S and Ctrl-Q are keys
            mode[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN)  # no line editing, echo or signals
            mode[6][termios.VMIN] = 1
            mode[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, mode)
        return self

    def __exit__(self, *exception):
        if os.name == "nt":
            self.kernel32.SetConsoleMode(self.handle, self.old_mode)
        else:
            self.termios.tcsetattr(self.fd, self.termios.TCSADRAIN, self.old_mode)

    def read(self):
        """The next key, as the bytes to send."""
        if os.name == "nt":
            key = self.msvcrt.getwch()
            if key in ("\x00", "\xe0"):             # arrow and function keys: a second code follows
                self.msvcrt.getwch()
                return b""                          # the Lua console doesn't use them
            return key.encode("utf-8")
        return os.read(self.fd, 1)


class Link:
    """The serial connection to the board, reopened whenever the board comes back."""

    def __init__(self, port):
        self.fixed_port = port
        self.serial = None
        self.lock = threading.Lock()
        self.running = True
        self.tail = b""                             # the last few bytes received, to spot a prompt

    def reader(self):
        """Background thread: copy everything the board sends to the screen."""
        out = sys.stdout.buffer
        while self.running:
            if self.serial is None:
                port = self.fixed_port or board.find_port()
                if port is None:
                    time.sleep(0.25)
                    continue
                try:
                    link = board.open_port(port, timeout=1)
                except SystemExit:
                    time.sleep(0.25)
                    continue
                with self.lock:
                    self.serial = link
                out.write(f"--- connected to {port} ---\r\n".encode())
                out.flush()
                self.write(b"\r")                   # ask for a fresh prompt
            try:
                data = self.serial.read(256)
            except (serial.SerialException, OSError):
                with self.lock:
                    self.serial.close()
                    self.serial = None
                out.write(b"\r\n--- board disconnected; waiting for it ---\r\n")
                out.flush()
                continue
            if data:
                out.write(data)
                out.flush()
                self.tail = (self.tail + data)[-3:]

    def wait_for_prompt(self, timeout):
        """Wait until the board's output ends with a prompt: "> " or ">> "."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not self.tail.endswith(b"> "):
            time.sleep(0.02)

    def write(self, data):
        with self.lock:
            if self.serial is not None:
                try:
                    self.serial.write(data)
                except (serial.SerialException, OSError):
                    pass                            # the reader notices and reconnects


def main():
    parser = argparse.ArgumentParser(description="A terminal for the C3U-Metal Lua console.")
    parser.add_argument("--port", help="the board's serial port, if it isn't found automatically")
    args = parser.parse_args()

    link = Link(args.port)
    threading.Thread(target=link.reader, daemon=True).start()

    if not sys.stdin.isatty():
        # Piped input: type each line when the board is ready for it, like a person would.
        lines = sys.stdin.buffer.read().replace(b"\r\n", b"\n").split(b"\n")
        link.wait_for_prompt(timeout=10)            # connected, and the first prompt is showing
        for line in lines:
            if line:
                link.tail = b""
                link.write(line + b"\r")
                link.wait_for_prompt(timeout=60)
        time.sleep(0.2)
        link.running = False
        return

    print("--- C3U-Metal monitor. Ctrl-] quits; Ctrl-C stops a running Lua program. ---")
    with Keyboard() as keyboard:
        while True:
            key = keyboard.read()
            if key == QUIT_KEY:
                break
            if key:
                link.write(key)
    link.running = False
    print()


if __name__ == "__main__":
    main()
