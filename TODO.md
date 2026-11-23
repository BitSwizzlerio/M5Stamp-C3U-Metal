# To do

Things that should be done, mostly for the safety of students who wire real
loads to the board. Each says why, and roughly what the fix is.

## A scan watchdog in the main firmware

C3U-Metal turns every hardware watchdog off at start-up
(`chip/system_esp32c3.c`), and nothing else notices when the ladder stops
scanning. If something holds the CPU (a long C function, a stuck `printf`, a
loop with interrupts off), the ladder's outputs stay frozen as they were,
possibly on. (A crash is covered: the crash report turns them off and the LED
red.) A real PLC has a scan watchdog: when a scan is overdue, it faults and
turns every output off.

- Watch the time since the last scan, from somewhere that keeps running when
  the main program doesn't: a timer interrupt (exercise 4), or one of the
  chip's own watchdogs (`TIMG0`'s MWDT, or the RTC watchdog) fed by each scan.
- On a fault: outputs off, the ladder stopped, and a message at the next
  prompt saying why. `plc.status()` and the viewer show it.
- Exercise 14 builds a scan watchdog on exercise 4's timer interrupt. The main
  firmware has no interrupts, so it needs another way, or the hardware
  watchdog.

## Say what `plc.run(1)` can really do

While the console waits at the prompt, the ladder scans as often as asked. While
Lua runs, scans can only start at Lua's hook, every 1000 instructions, and at
40 MHz that isn't often enough for 1 ms: on the board, 1,516 of 1,673 scans at
1 ms started late. `docs/ladder.md`, `plc.help()` and the manual say "1 to
1000 ms" without that caveat.

- Say so where the period is described, with the measurement, and point to
  `plc.status()`'s late count and to exercise 12 (scans from a timer interrupt).
