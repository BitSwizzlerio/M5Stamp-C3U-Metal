# Exercise 12: Ladder logic on a timer interrupt

The ladder logic engine ([docs/ladder.md](../ladder.md)) runs its scans by **polling**: the console calls `plc_poll()` while it waits for a key, every 1000 Lua instructions and all through `delay()`, and `plc_poll()` runs a scan when one is due. That works until Lua spends a long time somewhere that doesn't poll. Lua's garbage collector is the usual culprit.

Try it: load any program, run it, and give Lua some rubbish to collect. `plc_image` starts at `0x3FCBE000`; its scan count is at `0x3FCBE018` and its late-scan count at `0x3FCBE024` (`app/plc.h` has the whole layout).

```lua
plc.load("LD M0\nOUT M1") plc.run(10)
a = peek(0x3FCBE018) l = peek(0x3FCBE024) s = millis()
while millis() - s < 1000 do local t = {} for i = 1, 200 do t[i] = {i} end end
print("scans", peek(0x3FCBE018) - a, "late", peek(0x3FCBE024) - l)
```

A scan every 10 ms should make 100 scans a second. On the board this printed about 75 scans, 20 of them late. A real machine wouldn't wait for the garbage collector.

**Goal:** run the scans from exercise 4's 1 ms timer interrupt instead, so that they happen on time whatever Lua is doing. The same loop should print 100 scans and none late.

This exercise needs exercise 4: start from your own solution, or from `solutions/04-timer-interrupt`.

## 1. Scan from the interrupt

Exercise 4's `tick_handler()` (in `drivers/tick.c`) runs every millisecond. Make it call a new function, `plc_tick()`, in `app/plc.c`. Every `period_us / 1000` calls, `plc_tick()` runs a scan, as long as the ladder is running.

`plc_poll()` then has nothing to do. Leave it there with an empty body, so `app/repl.c` needn't change.

## 2. Don't call uptime_cycles() in the interrupt

`scan()` uses `uptime_cycles()` to work out how much time has passed for the timers. That function adds to a 64-bit total kept in a static variable, and the main program calls it too. What happens if the interrupt arrives in the middle of the main program's call, between reading the total and storing the new one?

Time the scans with `cycle_count()` instead: keep the count at the last scan in a `uint32_t` and subtract. The subtraction gives the right answer even when the counter wraps, as long as scans are less than 107 s apart.

## 3. Protect what the two share

Now two things change `plc_image`: the interrupt, and the main program when Lua calls `plc.load()`, `plc.run()`, `plc.stop()`, `plc.step()`, `plc.pull()` or `plc.set()`. Think about each one. What could a scan see if the interrupt arrived halfway through it?

- **`plc_install()`** copies a new program in. A scan halfway through would run half of the old program and half of the new.
- **`plc.set()`** sets one M bit with a read, a change and a write. A scan that writes M bits in between would have its change undone.
- **`plc_step()`** runs a scan itself. Two scans at once would share the same edge memory and timers.

Turn interrupts off around each of these with `cpu_interrupts_off()` and `cpu_interrupts_on()` from exercise 4. Keep those sections short: while interrupts are off, ticks and scans wait.

What about `plc.show()`, `plc.list()` and `plc.get()`? They only read. A scan in the middle could make a drawing show a mix of two scans. Is that worth turning interrupts off for?

## 4. Late scans

With polling, a scan was late if it started a whole period late. Count late scans in `scan()` instead: if more than the period plus one millisecond has passed since the last scan, interrupts were held off for that long. Don't count the scans `plc.step()` runs.

## Check

- `python tools/selftest.py` passes.
- The garbage collector loop above prints 100 or 101 scans and 0 late.
- `a = peek(0x3FCBE018) delay(1000) print(peek(0x3FCBE018) - a)` still prints 100.
- `examples/14-ladder-motor.lua` works as before: the button starts the motor and the fan follows.
- `for i = 1, 300 do led(0, i % 40, 0) end` while a ladder runs: the LED's timing is still protected (exercise 4's `sk6812.S` turns interrupts off while it sends).

## Going further

- A scan takes 25 to 200 µs. With a 1 ms period, how much of the CPU does the ladder use? Measure it: `plc.status()` shows the longest scan.
- Give the ladder its own SYSTIMER comparator, with its own period, instead of counting ticks.
- The C3U Viewer shows `late` in the Ladder Logic card's statistics. Watch it while Lua collects garbage, with and without your solution.
