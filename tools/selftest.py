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

    con.type("if plc then plc.stop() end")      # a running ladder would own GPIO4, which the checks drive

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

    out, _ = con.type("t = micros() delay(50) print(micros() - t > 40000)")
    check("micros() counts microseconds", out == "true", out)

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

    out, _ = con.type("gpio.output(4) poke(0x60004008, 1 << 4) a = gpio.read(4) "
                      "poke(0x6000400C, 1 << 4) print(a, gpio.read(4))")
    check("poke() into the GPIO registers drives GPIO4 high, then low", out == "1\t0", out)

    out, _ = con.type("poke(3, 0)")
    check("poke() refuses an address that is not a multiple of 4", "multiple of 4" in out, out)

    out, _ = con.type("poke(0x60043000, 0)")
    check("poke() refuses the USB console's registers", "USB console" in out, out)

    out, _ = con.type("gpio.output(18)")
    check("the USB pins are refused", "USB port" in out, out)

    out, _ = con.type("led(0, 0, 0)")
    check("led(0, 0, 0) is accepted", out == "", out)

    out, _ = con.type("led(300, 0, 0)")
    check("led() refuses 300", "must be 0-255" in out, out)

    out, _ = con.type("print(os, io)")
    check("no os or io library (there are no files)", out == "nil\tnil", out)

    out, _ = con.type("a = peek(0x600260B0) n = 0 for i = 1, 16 do "
                      "if peek(0x600260B0) ~= a then n = n + 1 end end print(n > 0)")
    check("the hardware random number generator gives different values", out == "true", out)

    out, _ = con.type("mem()")
    check("mem() reports memory, and the stack has not overflowed",
          "never claimed" in out and "OVERFLOWED" not in out, out)

    # Ladder logic (app/plc*.c). M bits and GPIO4 only, so nothing needs wiring.
    out, _ = con.type('plc.load("LD M0\\nOUT M1")')
    check("plc.load() assembles IL and lists the pins", out == "2 instructions in 1 rung. inputs: none  outputs: none", out)

    out, _ = con.type('plc.step() a = plc.get("M1") plc.set("M0", 1) plc.step() print(a, plc.get("M1"))')
    check("a ladder rung follows an M bit that Lua sets", out == "0\t1", out)

    out, _ = con.type('plc.load("LD M0\\nTON T0 100\\nOUT M2") plc.set("M0", 1) plc.step() '
                      'a = plc.get("M2") delay(150) plc.step() print(a, plc.get("T0"))')
    check("TON T0 100 turns on after 100 ms", out.endswith("0\t100\t1"), out)

    # pulse(m): turn an M bit on for one scan, then off for one, so a box sees the rung turn on once.
    con.type('function pulse(m) plc.set(m, 1) plc.step() plc.set(m, 0) plc.step() end')
    out, _ = con.type('plc.load("LD M0\\nTP T0 100\\nOUT M2") plc.set("M0", 1) plc.step() a = plc.get("M2") '
                      'plc.set("M0", 0) delay(50) plc.step() b = plc.get("M2") delay(80) plc.step() '
                      'print(a, b, plc.get("M2"))')
    check("TP T0 100 gives a 100 ms pulse, even after the rung goes off", out.endswith("1\t1\t0"), out)

    out, _ = con.type('plc.load("LD M0\\nRTO T0 100\\nOUT M2") plc.set("M0", 1) plc.step() delay(60) plc.step() '
                      'plc.set("M0", 0) plc.step() delay(50) plc.step() held = plc.get("T0") '
                      'plc.set("M0", 1) plc.step() delay(60) plc.step() print(held >= 55 and held < 100, plc.get("T0"))')
    check("RTO keeps its time while the rung is off, and adds to it", out.endswith("true\t100\t1"), out)

    out, _ = con.type('plc.load("LD M0\\nCTD C0 3\\nOUT M2") a = plc.get("C0") pulse("M0") pulse("M0") '
                      'b = plc.get("M2") pulse("M0") print(a, b, plc.get("C0"))')
    check("CTD alone counts down from its preset, and is done at 0", out.endswith("3\t0\t0\t1"), out)

    out, _ = con.type('plc.load("LD M0\\nCTU C0 5\\nOUT M2\\nLD M1\\nCTD C0 5\\nOUT M3") '
                      'pulse("M0") pulse("M0") pulse("M0") pulse("M1") print(plc.get("C0"))')
    check("a CTU and a CTD on one counter count up and down", out.endswith("2\t0"), out)

    out, _ = con.type('plc.load("LD M0\\nCTU C0 5\\nOUT M2\\nLD M1\\nCTD C0 6\\nOUT M3")')
    check("a CTU and a CTD on one counter must have the same preset", "give them the same count" in out, out)

    out, _ = con.type('plc.load("LD M0\\nADD D0 5 D0\\nLD>= D0 10\\nOUT M1") plc.set("M0", 1) plc.step() '
                      'a = plc.get("M1") plc.step() print(plc.get("D0"), a, plc.get("M1"))')
    check("ADD adds every scan, and LD>= compares", out.endswith("\n10\t0\t1"), out)

    out, _ = con.type('plc.load("LD< D0 0\\nOUT M1\\nLD M0\\nDIV D0 -3 D1") plc.set("D0", -7) plc.set("M0", 1) '
                      'plc.step() print(plc.get("M1"), plc.get("D1"))')
    check("Lua sets D0; a negative compares, and DIV rounds towards 0", out.endswith("\n1\t2"), out)

    out, _ = con.type('plc.load("LD M0\\nMOV 5 7")')
    check("a result has to go into a D register", "has to go into a register" in out, out)

    # The viewer's commands (PLC_CMD_* in app/plc.h): 'command' is at 0x3FCBE000 + 7392.
    out, _ = con.type('plc.load("LD M0\\nOUT M1") plc.run() poke(0x3FCBFCE0, 3 | (7 + 256) << 8) delay(20) '
                      'a = plc.get("M7") poke(0x3FCBFCE0, 2) delay(20) print(a, peek(0x3FCBE00C), peek(0x3FCBFCE0))')
    check("commands written into plc_image set an M bit and stop the ladder", out.endswith("\n1\t0\t0"), out)

    out, _ = con.type('plc.load("LD M0\\nLDD G4")')
    check("plc.load() names the line of a mistake", "line 2: unknown instruction 'LDD'" in out, out)

    out, _ = con.type('plc.load("LD M0\\nOUT G9")')
    check("the ladder can't drive the button's pin", "G9 is the button" in out, out)

    out, _ = con.type('plc.load("LD G12\\nOUT G4")')
    check("the ladder refuses the flash chip's pins", "flash chip" in out, out)

    out, _ = con.type('print(hex(peek(0x3FCBE000)))')
    check("plc_image is where the viewer looks for it", out == "0x5244414c", out)

    con.type('plc.load("LD M0\\nOUT G4") plc.run(10)')
    out, _ = con.type("gpio.write(4, 1)")
    check("gpio.write() refuses a pin the ladder drives", "G4 is a ladder output" in out, out)

    out, _ = con.type('plc.set("M0", 1) delay(50) a = gpio.read(4) plc.set("M0", 0) delay(50) print(a, gpio.read(4))')
    check("the running ladder drives GPIO4 high, then low", out == "1\t0", out)

    out, _ = con.type("t = peek(0x3FCBE018) s = millis() while millis() - s < 200 do end "
                      "print(peek(0x3FCBE018) - t >= 15)")
    check("the ladder keeps scanning while a Lua loop runs", out == "true", out)

    out, _ = con.type("plc.stop()")
    check("plc.stop() turns the ladder's outputs off", out == "ladder stopped; G4 set to 0", out)

    out, _ = con.type("gpio.output(4) gpio.write(4, 1) a = gpio.read(4) gpio.write(4, 0) print(a)")
    check("after plc.stop(), GPIO4 belongs to Lua again", out == "1", out)

    # Last, because it restarts the board. Only the CPU is reset, so the port stays
    # open and the ROM's boot message and the banner arrive on the connection we
    # already have. Waiting for both also proves it was a software CPU reset.
    con.link.reset_input_buffer()
    con.link.write(b"reset()\r")
    con.link.flush()
    banner = b""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and b"Press Enter for a prompt" not in banner:
        banner += con.link.read(256)
    check("reset() restarts the board without dropping the console",
          b"RTC_SW_CPU_RST" in banner and b"Press Enter for a prompt" in banner, banner)
    con.type("")                            # back to a prompt

    passed = sum(results)
    print(f"\n{passed} of {len(results)} checks passed")
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
