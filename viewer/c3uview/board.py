"""
board.py - the board's own numbers, from C3U-Metal's board_status.

The firmware (app/status.c) keeps uptime, heap and stack use, the reason the
CPU last started and its build date in one struct, board_status, at a fixed
address (0x3FCBDF00, set in boot/c3u-metal.ld), and updates it four times a
second. The offsets below follow struct board_status in app/status.h. The
viewer reads it once a second, as part of a frame's read: 18 words, about 5 ms.
Reading it changes nothing on the board.
"""
import struct

BASE = 0x3FCBDF00
MAGIC = 0x54415453                  # "STAT"
LAYOUT = 1
WORDS = 18                          # 12 numbers, then 'built', 24 characters

# The CPU's reset reason, from ESP-IDF's components/soc/esp32c3/include/soc/reset_reasons.h.
RESET_REASONS = {
    0x01: "power on",
    0x03: "a software reset of the chip",
    0x05: "waking from deep sleep",
    0x07: "watchdog (timer group 0)", 0x08: "watchdog (timer group 1)", 0x09: "the RTC watchdog",
    0x0B: "watchdog (timer group 0)", 0x0C: "reset() or a software reset of the CPU",
    0x0D: "the RTC watchdog", 0x0F: "the supply voltage dipping (brown-out)", 0x10: "the RTC watchdog",
    0x11: "watchdog (timer group 1)", 0x12: "the super watchdog", 0x13: "a glitch on the clock",
    0x14: "an eFuse error", 0x15: "the USB port (flashing, or a terminal's reset)",
    0x16: "the USB port (JTAG)", 0x17: "a glitch on the power",
}


def plan(frame):
    """The block to read with this frame's pins: once a second at 20 frames a second."""
    return [("board", BASE, WORDS)] if frame % 20 == 5 else []


def decode(regs):
    """A 'board' event from this frame's reads, or None if board_status wasn't read this time."""
    words = regs.get("board")
    if words is None:
        return None
    if words[0] != MAGIC or words[1] != LAYOUT:
        return {"type": "board", "present": False}
    built = struct.pack("<6I", *words[12:18]).split(b"\0", 1)[0].decode("ascii", errors="replace")
    reason = words[5]
    return {
        "type": "board", "present": True, "updates": words[2], "uptime_ms": words[3], "cpu_mhz": words[4],
        "reset_reason": reason, "reset_text": RESET_REASONS.get(reason, f"reason {reason}"),
        "heap_size": words[6], "heap_in_use": words[7], "heap_claimed": words[8],
        "stack_size": words[9], "stack_max_used": words[10], "stack_overflowed": bool(words[11]),
        "built": built,
    }
