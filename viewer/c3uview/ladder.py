"""
ladder.py - what to read of the board's ladder logic engine, and what it means.

C3U-Metal's ladder engine (app/plc.c) keeps everything it knows in one struct,
plc_image, at a fixed address (0x3FCBE000, set in boot/c3u-metal.ld). The
offsets below follow struct plc_image in app/plc.h; PLC_LAYOUT there says which
version of it this is, and a board whose image doesn't start with MAGIC and
LAYOUT has no ladder engine this viewer understands.

Reads are kept small, because every word costs time on JTAG:
  - with the Ladder Logic card closed, 5 words every 10th frame, to show whether
    a ladder is running;
  - with it open, every 2nd frame, the header and M bits, the trace bits the
    program uses, and only the timers and counters it uses;
  - the program and its IL text only after plc.load() (when 'loads' changes).
Reading plc_image changes nothing on the board.

The viewer writes only one word: 'command', to start or stop the ladder or set
an M bit (PLC_CMD_* in plc.h). It writes one only while 'command' reads 0, and
the board's plc_poll() carries it out between scans and sets it back to 0. That
works even while a Lua program keeps the console too busy to type into.
"""
import struct

BASE = 0x3FCBE000
MAGIC = 0x5244414C                  # "LADR"
LAYOUT = 2

# Offsets in bytes, from struct plc_image in app/plc.h.
HEADER = 0                          # magic, layout, loads, running, claimed, period_us, scans, scan_us,
HEADER_WORDS = 14                   # scan_us_max, late, in_mask, out_mask, inputs, outputs
M = 56                              # M0-M255: 8 words
M_WORDS = 8
TIMERS = 88                         # 32 x (elapsed_us, preset_ms, done, last)
COUNTERS = 600                      # 32 x (count, preset, done, last)
TC_STRIDE = 16
TRACE = 1112                        # one bit per program word
WORDS = 1240                        # program length in words
PROGRAM = 1244                      # up to 512 words
SOURCE_LEN = 3292
SOURCE = 3296                       # up to 4096 bytes
MAX_WORDS = 512
MAX_SOURCE = 4095
COMMAND = 7392                      # layout 2: written by the viewer, 0 once the board has carried it out
CMD_RUN, CMD_STOP, CMD_SET_M = 1, 2, 3


def command_word(name, period_ms=10, m=0, value=0):
    """The word to write into 'command' for "run", "stop" or "set" (an M bit)."""
    if name == "run":
        return CMD_RUN | max(1, min(1000, int(period_ms))) << 8
    if name == "stop":
        return CMD_STOP
    if name == "set":
        return CMD_SET_M | ((int(m) & 0xFF) | (0x100 if value else 0)) << 8
    raise ValueError(name)

# The instructions, in the order of enum plc_op in app/plc.h.
OPS = ["LD", "LDN", "AND", "ANDN", "OR", "ORN", "ANB", "ORB", "OUT",
       "LDP", "LDF", "ANDP", "ANDF", "ORP", "ORF", "SET", "RST", "TON", "TOF", "CTU"]
BOXES = {"TON", "TOF", "CTU"}       # these take a second word: the preset
KIND_T, KIND_C = 2, 3


class State:
    """What the viewer knows about the program on the board."""

    def __init__(self):
        self.loads = None           # 'loads' when the program below was read, or None
        self.words = 0
        self.timers = []            # timer and counter numbers the program uses
        self.counters = []

    def learn(self, loads, program):
        self.loads = loads
        self.words = len(program)
        timers, counters = set(), set()
        i = 0
        while i < len(program):
            word = program[i]
            op = word & 0x3F
            kind, index = (word >> 6) & 3, (word >> 8) & 0xFF
            if kind == KIND_T:
                timers.add(index)
            elif kind == KIND_C:
                counters.add(index)
            i += 2 if op < len(OPS) and OPS[op] in BOXES else 1
        self.timers, self.counters = sorted(timers), sorted(counters)


def plan(frame, card_open, state):
    """The ladder blocks to read with this frame's pins: [(name, address, words)]."""
    if not card_open:
        return [("plc_head", BASE + HEADER, 5)] if frame % 10 == 0 else []
    if frame % 2:
        return []
    blocks = [("plc_head", BASE + HEADER, HEADER_WORDS + M_WORDS)]
    if state.loads is not None and state.words:
        blocks.append(("plc_trace", BASE + TRACE, (state.words + 31) // 32))
        for n in state.timers:
            blocks.append((f"plc_t{n}", BASE + TIMERS + n * TC_STRIDE, 3))
        for n in state.counters:
            blocks.append((f"plc_c{n}", BASE + COUNTERS + n * TC_STRIDE, 3))
    return blocks


def decode(regs, state):
    """A 'ladder' event from this frame's reads, or None if the ladder wasn't read this time."""
    head = regs.get("plc_head")
    if head is None:
        return None
    if head[0] != MAGIC or head[1] != LAYOUT:
        return {"type": "ladder", "present": False}
    event = {"type": "ladder", "present": True, "loads": head[2], "running": bool(head[3]),
             "claimed": bool(head[4]), "detail": len(head) > 5}
    if len(head) > 5:
        event.update(period_ms=head[5] // 1000, scans=head[6], scan_us=head[7], scan_us_max=head[8],
                     late=head[9], in_mask=head[10], out_mask=head[11], inputs=head[12], outputs=head[13],
                     m=head[HEADER_WORDS:HEADER_WORDS + M_WORDS])
        event["current"] = state.loads == head[2]       # the trace and timers belong to the program we have
        if event["current"]:
            event["trace"] = regs.get("plc_trace", [])
            event["timers"] = {n: _tc(regs.get(f"plc_t{n}"), 1000) for n in state.timers}
            event["counters"] = {n: _tc(regs.get(f"plc_c{n}"), 1) for n in state.counters}
    return event


def _tc(words, divide):
    """[elapsed ms or count, preset, done] for one timer or counter."""
    if not words:
        return [0, 0, 0]
    return [words[0] // divide, words[1], words[2]]


def read_program(jtag):
    """Read the program and its IL text, in two short reads. A 'ladder-program' event, or None."""
    sizes = jtag.read([("plc_head", BASE + HEADER, 3), ("plc_words", BASE + WORDS, 1),
                       ("plc_source_len", BASE + SOURCE_LEN, 1)])
    if not sizes or sizes["plc_head"][0] != MAGIC or sizes["plc_head"][1] != LAYOUT:
        return None
    loads = sizes["plc_head"][2]
    words = min(sizes["plc_words"][0], MAX_WORDS)
    length = min(sizes["plc_source_len"][0], MAX_SOURCE)
    blocks = [("plc_head", BASE + HEADER, 3)]
    if words:
        blocks.append(("plc_program", BASE + PROGRAM, words))
    if length:
        blocks.append(("plc_source", BASE + SOURCE, (length + 3) // 4))
    data = jtag.read(blocks)
    if not data or data["plc_head"][2] != loads:        # a plc.load() happened in between: try again later
        return None
    program = data.get("plc_program", [])
    raw = struct.pack(f"<{len(data.get('plc_source', []))}I", *data.get("plc_source", []))
    source = raw[:length].decode("utf-8", errors="replace")
    return {"type": "ladder-program", "loads": loads, "program": program, "source": source}
