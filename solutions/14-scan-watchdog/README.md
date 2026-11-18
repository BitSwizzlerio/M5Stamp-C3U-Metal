# Solution to exercise 14: A scan watchdog

This solution builds on exercise 4, so it contains all of `04-timer-interrupt`'s files as well as its own.

- **Changed files:** `app/plc.c`, `app/plc.h`, `app/lua_plc.c`, `app/repl.c`, `app/trap_report.c`, `drivers/tick.c`.
- **From exercise 4:** `chip/interrupts.c`, `chip/interrupts.h`, `chip/trap.S`, `chip/cpu.S`, `chip/cpu.h`, `chip/esp32c3-regs.h`, `drivers/sk6812.S`, `drivers/tick.h`, `app/lua_hw.c`, `app/main.c`.

## How it fits together

```
SYSTIMER comparator 0, every 1 ms (drivers/tick.c)
  -> tick_handler() -> plc_watchdog() (app/plc.c)
  -> no scan for watchdog_ms while running: outputs off, ladder stopped, fault noted

the console, before each prompt (app/repl.c)
  -> plc_report(): prints the fault, once
```

- **`app/plc.c`**:
  - `ms_since_scan` counts milliseconds: `plc_watchdog()` adds 1, and `scan()` sets it to 0. Both are single 32-bit loads and stores, and the worst a badly timed interrupt can do is count one millisecond too many.
  - `plc_watchdog()` does nothing unless the ladder is running and the watchdog is on. When the count reaches `watchdog_ms` (200 to start with), it calls `plc_outputs_off()`, clears `running` and `claimed`, and keeps the count in `fault_ms`.
  - `scan()` turns interrupts off around its last step, the check of `claimed` and the two writes to the outputs, so a trip in between can't turn the outputs back on.
  - `plc_report()` prints the fault once. `plc_run()` clears it.
- **`app/repl.c`**: `push_line()` calls `plc_report()` before it shows a prompt, so the message never lands in the middle of a line.
- **`app/trap_report.c`**: a crash leaves interrupts off, so the watchdog can't run. `trap_report()` calls `plc_outputs_off()` before it prints the report.
- **`app/lua_plc.c`**: `plc.watchdog(ms)`, 0 or 20 to 60000; `plc.watchdog()` returns it. `plc.status()` shows the watchdog and any fault.
- **`drivers/tick.c`**: `tick_handler()` calls `plc_watchdog()`.

The watchdog isn't kept in `plc_image`, so the layout and the C3U Viewer don't change.

## Tested on the board

`LD M0 / OUT G4` with M0 set, scanning every 10 ms, and G4 read over JTAG every few milliseconds from the PC while the board worked:

| Check | Result |
|---|---|
| `string.rep("a", 600):find(".-b")` | blocked for 577 ms; G4 went off about 200 ms in; the fault message came before the next prompt |
| `plc.status()` after it | "stopped by the watchdog, after 200 ms without a scan" |
| 1 s of garbage collection, then `collectgarbage()` (11 ms) | still running, G4 still on |
| `crash()` | the crash report, and G4 off |
| `python tools/selftest.py` | 44 of 44 |
