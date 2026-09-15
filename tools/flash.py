#!/usr/bin/env python3
"""
flash.py - write C3U-Metal to the M5Stamp C3U.

    python tools/flash.py                   writes build/c3u-metal.bin
    python tools/flash.py other.bin         writes another image
    python tools/flash.py --port COM5       if the board isn't found by itself

It finds the board by its USB IDs and runs esptool, which writes the image at
flash offset 0x0. That is where the ESP32-C3's ROM looks for a direct-boot
program. esptool then resets the board, and the new program starts.
"""
import argparse
import os
import subprocess
import sys

import board

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    parser = argparse.ArgumentParser(description="Write a program image to the M5Stamp C3U.")
    parser.add_argument("image", nargs="?", default=os.path.join(ROOT, "build", "c3u-metal.bin"),
                        help="the raw image to write (default: build/c3u-metal.bin)")
    parser.add_argument("--port", help="the board's serial port, if it isn't found automatically")
    args = parser.parse_args()

    if not os.path.isfile(args.image):
        sys.exit(f"{args.image} not found. Build it first: cmake --build build")
    port = args.port or board.wait_for_port(timeout=3)
    print(f"Found M5Stamp C3U on {port}")

    # esptool is a Python package, so run it with this same Python.
    command = [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", port,
               "write-flash", "0x0", args.image]
    result = subprocess.run(command)
    if result.returncode != 0:
        print("\nFlashing failed. If the port is busy, close any serial monitor that is using it.\n"
              "If the board doesn't answer, unplug it, hold its button while plugging it back in, "
              "and try again.", file=sys.stderr)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
