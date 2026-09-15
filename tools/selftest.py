#!/usr/bin/env python3
"""
selftest.py - check that the board runs C3U-Metal correctly.

    python tools/selftest.py [--port PORT]

It types Lua into the board's console, checks each answer and prints PASS or
FAIL. The exit status is 1 if anything failed, so scripts can use it too.

Before running it:
- leave GPIO4 unconnected, because the test drives it and reads it back;
- don't press the button;
- close any serial monitor.
"""
import argparse
import re
import sys
import time

import board
import serial

PROMPT = re.compile(rb"\r\n(>>?) $")        # the console's prompts: "> ", or ">> " inside a statement


class Console:
    def __init__(self, link):
        self.link = link

    def type(self, line, timeout=5.0):
        """Type a line and press Enter. Returns (output, prompt); the board's echo of the line is removed."""
        self.link.reset_input_buffer()
        self.link.write(line.encode() + b"\r")
        return self.wait_for_prompt(timeout)

    def interrupt(self, line, after=0.5):
        """Type a line that runs forever, then press Ctrl-C."""
        self.link.reset_input_buffer()
        self.link.write(line.encode() + b"\r")
        time.sleep(after)
        self.link.write(b"\x03")
        return self.wait_for_prompt(5.0)

    def wait_for_prompt(self, timeout):
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            received += self.link.read(256)
            match = PROMPT.search(received)
            if match:
                text = received[:match.start()].decode("utf-8", "replace").replace("\r\n", "\n")
                echo_and_output = text.split("\n", 1)
                output = echo_and_output[1] if len(echo_and_output) > 1 else ""
                return output, match.group(1).decode()
        raise TimeoutError(f"no prompt after {timeout} s; received {received!r}")


def connect(port_arg):
    """Open the console and wait for a prompt. Retries while the board is still starting after a reset."""
    deadline = time.monotonic() + 15
    while True:
        try:
            port = port_arg or board.wait_for_port(timeout=10)
            link = board.open_port(port)
            console = Console(link)
            time.sleep(0.2)
            console.type("", timeout=2.0)
            return port, console
        except (serial.SerialException, OSError, TimeoutError):
            if time.monotonic() > deadline:
                sys.exit("The board did not answer. Is C3U-Metal flashed? (cmake --build build -t flash)")
            time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser(description="Check that the board runs C3U-Metal correctly.")
    parser.add_argument("--port", help="the board's serial port, if it isn't found automatically")
    args = parser.parse_args()

    port, con = connect(args.port)
    print(f"Testing the board on {port}\n")
    results = []

    def check(name, ok, got):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
        if not ok:
            print(f"      got: {got!r}")

    out, _ = con.type("print(type(gpio), type(help))")
    check("C3U-Metal's Lua functions are there", out == "table\tfunction", out)

    out, _ = con.type("print(1 + 2)")
    check("print(1 + 2) prints 3", out == "3", out)

    out, _ = con.type("print(7 // 2, 7 / 2, math.maxinteger)")
    check("32-bit integers and floats", out == "3\t3.5\t2147483647", out)

    _, first = con.type("for i = 1, 3 do")
    _, second = con.type("print(i)")
    out, last = con.type("end")
    check("a statement over several lines", (first, second, last, out) == (">>", ">>", ">", "1\n2\n3"),
          (first, second, last, out))

    out, _ = con.type('error("boom")')
    check("errors print a message and a traceback", "boom" in out and "stack traceback" in out, out)

    out, _ = con.interrupt("while true do end")
    check("Ctrl-C stops a program that never ends", "interrupted!" in out, out)

    out, _ = con.type("t = millis() delay(250) print(millis() - t)")
    check("delay(250) takes 250 ms", out.isdigit() and 245 <= int(out) <= 275, out)

    out, _ = con.type("print(button())")
    check("button() is false while not pressed", out == "false", out)

    out, _ = con.type("gpio.output(4) gpio.write(4, 1) a = gpio.read(4) gpio.write(4, 0) print(a, gpio.read(4))")
    check("GPIO4 as an output reads back 1, then 0", out == "1\t0", out)

    con.type('gpio.input(4, "up")')
    up, _ = con.type("print(gpio.read(4))")
    con.type('gpio.input(4, "down")')
    down, _ = con.type("print(gpio.read(4))")
    con.type("gpio.input(4)")
    check("GPIO4 reads 1 with its pull-up and 0 with its pull-down", (up, down) == ("1", "0"), (up, down))

    out, _ = con.type("gpio.output(18)")
    check("the USB pins are refused", "USB port" in out, out)

    out, _ = con.type("led(0, 0, 0)")
    check("led(0, 0, 0) is accepted", out == "", out)

    out, _ = con.type("led(300, 0, 0)")
    check("led() refuses 300", "must be 0-255" in out, out)

    out, _ = con.type("print(os, io)")
    check("no os or io library (there are no files)", out == "nil\tnil", out)

    out, _ = con.type("mem()")
    check("mem() reports memory, and the stack has not overflowed",
          "never claimed" in out and "OVERFLOWED" not in out, out)

    passed = sum(results)
    print(f"\n{passed} of {len(results)} checks passed")
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
