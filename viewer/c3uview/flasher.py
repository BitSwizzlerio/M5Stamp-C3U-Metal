"""
flasher.py - write a program image onto the board with esptool.

The same as tools/flash.py in the project: the ESP32-C3's ROM starts a program
that sits at flash offset 0, so the image goes there. With erase, the whole
flash is cleared first, which also removes a Lua script saved by exercise 7.
"""
import contextlib
import io

import esptool


class _Lines(io.TextIOBase):
    """Collects what esptool prints and passes it on a line at a time."""

    def __init__(self, report):
        self.report = report
        self.buffer = ""

    def write(self, text):
        self.buffer += text.replace("\r", "\n")
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            if line.strip():
                self.report(line.rstrip())
        return len(text)

    def flush(self):
        pass


def _esptool(args, report):
    out = _Lines(report)
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            esptool.main(args)
        return True
    except SystemExit as stop:
        return stop.code in (0, None)
    except Exception as error:          # esptool raises its own errors for a missing or busy board
        report(f"error: {error}")
        return False
    finally:
        out.write("\n")


def flash(port, image_path, erase, report):
    """Write image_path at offset 0. Returns True if it worked."""
    base = ["--chip", "esp32c3", "--port", port]
    if erase:
        report("Erasing the whole flash...")
        if not _esptool(base + ["erase-flash"], report):
            return False
    report(f"Writing {image_path}...")
    return _esptool(base + ["write-flash", "0x0", image_path], report)
