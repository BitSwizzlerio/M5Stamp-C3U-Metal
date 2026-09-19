#!/usr/bin/env python3
"""
flash.py - write C3U-Metal to the M5Stamp C3U.

    python tools/flash.py                   writes build/c3u-metal.bin
    python tools/flash.py other.bin         writes another image
    python tools/flash.py --port COM5       if the board isn't found by itself
    python tools/flash.py --erase           erase the whole flash first: this also removes a
                                            script saved with exercise 7 that stops the board starting

It finds the board by its USB IDs and runs esptool, which writes the image at
flash offset 0x0. That is where the ESP32-C3's ROM looks for a direct-boot
program. esptool then resets the board, and the new program starts.
"""
import argparse
import os
import subprocess
import sys

import board

try:
    import esptool                      # only to check that it is there, and which version
except ImportError:
    sys.exit(f"esptool is not installed for {sys.executable}.\n"
             "Run this from an ESP-IDF terminal. If the build folder was set up outside one, delete it and "
             "run cmake --preset default from an ESP-IDF terminal, so that CMake finds that Python.")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    parser = argparse.ArgumentParser(description="Write a program image to the M5Stamp C3U.")
    parser.add_argument("image", nargs="?", default=os.path.join(ROOT, "build", "c3u-metal.bin"),
                        help="the raw image to write (default: build/c3u-metal.bin)")
    parser.add_argument("--port", help="the board's serial port, if it isn't found automatically")
    parser.add_argument("--erase", action="store_true",
                        help="erase the whole flash first; this also removes anything else kept there, "
                             "such as a script saved with exercise 7")
    args = parser.parse_args()

    if not os.path.isfile(args.image):
        sys.exit(f"{args.image} not found. Build it first: cmake --build build")
    port = args.port or board.wait_for_port(timeout=3)
    print(f"Found M5Stamp C3U on {port}")

    # esptool is a Python package, so run it with this same Python. esptool 5 (ESP-IDF 6)
    # spells its commands with dashes; esptool 4 (ESP-IDF 5) only knows the underscores.
    dashes = int(esptool.__version__.split(".")[0]) >= 5
    run = [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", port]
    if args.erase:
        result = subprocess.run(run + ["erase-flash" if dashes else "erase_flash"])
        if result.returncode != 0:
            sys.exit(result.returncode)
    result = subprocess.run(run + ["write-flash" if dashes else "write_flash", "0x0", args.image])
    if result.returncode != 0:
        print("\nFlashing failed. If the port is busy, close any serial monitor that is using it.\n"
              "If the board doesn't answer, unplug it, hold its button while plugging it back in, "
              "and try again.", file=sys.stderr)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
