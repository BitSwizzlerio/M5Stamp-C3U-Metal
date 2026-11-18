# Solution to exercise 12: Ladder logic on a timer interrupt

This solution builds on exercise 4, so it contains all of `04-timer-interrupt`'s files as well as its own.

- **Changed files:** `app/plc.c`, `app/plc.h`, `app/lua_plc.c`, `drivers/tick.c`.
- **From exercise 4:** `chip/interrupts.c`, `chip/interrupts.h`, `chip/trap.S`, `chip/cpu.S`, `chip/cpu.h`, `chip/esp32c3-regs.h`, `drivers/sk6812.S`, `drivers/tick.h`, `app/lua_hw.c`, `app/main.c`.

## How it fits together

```
SYSTIMER comparator 0, every 1 ms (drivers/tick.c)
  -> tick_handler() -> plc_tick() (app/plc.c)
  -> every period_us / 1000 ticks: scan()
```

- **`drivers/tick.c`**: `tick_handler()` calls `plc_tick()` after counting the tick.
- **`app/plc.c`**:
  - `plc_tick()` counts ticks down from the period and runs `scan()` when it reaches 0, as long as the ladder is running. `plc_poll()` is empty: the console still calls it, but there's nothing left for it to do.
  - `scan()` times itself with `cycle_count()` instead of `uptime_cycles()`, because `uptime_cycles()` updates a 64-bit total that the main program updates too. The difference of two 32-bit counts is right across a wrap.
  - A scan more than a period plus 1 ms after the last one counts as late: interrupts were held off for that long.
  - `plc_install()`, `plc_run()`, `plc_stop()`, `plc_step()` and `plc_set_pull()` turn interrupts off while they change `plc_image`, so a scan never sees half a program or runs at the same time as `plc.step()`.
- **`app/lua_plc.c`**: `plc.set()` turns interrupts off for its read, change and write of an M bit.
- **`app/plc.h`** declares `plc_tick()`.

`plc.show()`, `plc.list()` and `plc.get()` only read, so they leave interrupts on. A drawing taken during a scan may mix two scans, which is harmless.

## Tested on the board

A program scanning every 10 ms, and a Lua loop that makes garbage for 1 s:

```lua
a = peek(0x3FCBE018) l = peek(0x3FCBE024) s = millis()
while millis() - s < 1000 do local t = {} for i = 1, 200 do t[i] = {i} end end
print("scans", peek(0x3FCBE018) - a, "late", peek(0x3FCBE024) - l)
```

| Check | Polling (the project) | This solution |
|---|---|---|
| scans in that second, three runs | 78, 73, 74 | 101, 100, 101 |
| late scans | 17, 21, 20 | 0, 0, 0 |
| scans in 1 s of `delay(1000)` | 100 | 100 or 101 |
| `python tools/selftest.py` | 42 of 42 | 42 of 42 |
