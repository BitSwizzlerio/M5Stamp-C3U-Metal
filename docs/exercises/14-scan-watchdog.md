# Exercise 14: A scan watchdog

The ladder only scans when something calls `plc_poll()` (exercise 12 explains where). If the CPU gets stuck somewhere that doesn't call it, the scans stop, and the ladder's outputs stay exactly as they were. A motor that was running keeps running, whatever its stop button says.

Try it with an LED on G4, or watch G4 in the C3U Viewer:

```lua
plc.load("LD M0\nOUT G4") plc.set("M0", 1) plc.run()
string.rep("a", 600):find(".-b")       -- about half a second inside one C function
```

`find` with that pattern tries every start position and every length, all in C, so Lua's hook never runs and neither does `plc_poll()`. On the board it took 577 ms, and no scan ran. With 1000 characters it takes 1.5 s, and with 3000, 14 s. A real program gets stuck in less obvious ways: a long `print` with no terminal reading it, a sum that takes longer than expected, a crash.

A real PLC has a **watchdog**: a timer that each scan resets. If it ever runs out, the PLC stops, turns every output off and reports a fault.

**Goal:** a scan watchdog. If the ladder is running and no scan has come for 200 ms, turn its outputs off, stop it, and say so at the next prompt. `plc.watchdog(ms)` changes the time, and 0 turns it off.

This exercise needs exercise 4: start from your own solution, or from `solutions/04-timer-interrupt`. The watchdog must keep running while the main program is stuck, and only an interrupt can do that.

## 1. Count the time since the last scan

Give `app/plc.c` a counter of the milliseconds since the last scan. `scan()` sets it to 0. A new function, `plc_watchdog()`, adds 1, and exercise 4's `tick_handler()` (in `drivers/tick.c`) calls it every millisecond.

The counter is shared between the interrupt and the main program, so declare it `volatile`. Is anything else needed? Think about what each side does with it: one writes 0, the other reads, adds 1 and writes. What's the worst that can happen if the interrupt arrives in the middle of `scan()` setting it to 0?

## 2. Trip

When the counter reaches the watchdog's time, and the ladder is running, `plc_watchdog()`:

- turns every ladder output off at once: one write of `out_mask` to `GPIO_OUT_W1TC`;
- clears `running` and `claimed`, as `plc.stop()` would, so Lua may use the pins again;
- remembers how long it waited, as the fault.

Is that safe to do from inside the interrupt? It touches the pins and `plc_image`, and the main program might be in the middle of a scan. Look at the end of `scan()`: it checks `claimed`, then writes the outputs. What if the watchdog trips between the check and the writes? Turn interrupts off around that part of `scan()`, with `cpu_interrupts_off()` and `cpu_interrupts_on()`.

## 3. Say what happened

An interrupt shouldn't `printf()`: the main program may be printing too, and printing can wait for the USB host. Instead, add `plc_report()`, which prints the fault once, and call it from `push_line()` in `app/repl.c`, just before the prompt:

```
ladder stopped by the watchdog: no scan for 200 ms. Its outputs are off; plc.run() starts it again.
```

`plc.status()` says so too, and `plc.run()` clears the fault.

## 4. A crash

Try `crash()` with the ladder running and G4 on. The crash report appears, G4 goes off and the RGB LED turns red. That isn't your watchdog's doing: `trap_report()` in `app/trap_report.c` calls `plc_outputs_off()` before it prints anything. Why couldn't the watchdog have caught a crash? Look at what the CPU does with interrupts when it takes an exception, and at what `trap_report()` does afterwards.

The crash report is the last thing the program ever does, so it turns the outputs off itself. Use the same `plc_outputs_off()` in `plc_watchdog()`.

## 5. The Lua function

Add `plc.watchdog(ms)` and `plc.watchdog()`, which returns the time, to `app/lua_plc.c`, and a line to `plc.help()`. Refuse times under 20 ms: Lua's hook comes only every few milliseconds while Lua runs, so a shorter watchdog would trip on a busy Lua program.

## Check

- The `find` above: the console prints the fault before the next prompt, `plc.status()` says "stopped by the watchdog", and G4 is off. In the worked solution G4 went off about 200 ms into the 577 ms.
- No false alarms. With the ladder running, a second of garbage collection and a full collection leave it running:

  ```lua
  s = millis() while millis() - s < 1000 do local t = {} for i = 1, 200 do t[i] = {i} end end
  collectgarbage() plc.status()
  ```

- `crash()` with G4 on: G4 goes off and the LED turns red (the project's crash report does that). Unplug the board and plug it back in afterwards.
- `plc.watchdog(0)` turns it off: the `find` leaves G4 on again.
- `python tools/selftest.py` passes.

## Going further

- The tick interrupt keeps running while the main program is stuck, but what if interrupts are off? A loop that never turns them back on stops the watchdog too. The chip has its own watchdogs for that: `TIMG0`'s MWDT and the RTC watchdog, which C3U-Metal turns off at start-up (`chip/system_esp32c3.c`). Leave the MWDT on, feed it from each scan, and set it to reset the chip. Which is better for a machine: outputs off and a message, or a reset?
- How long should the watchdog wait? Too short and it trips when nothing is wrong; too long and a stuck output stays on longer. Measure the longest gap between scans while Lua works hard, and choose from that.
- The C3U Viewer shows "Ladder: stopped" after a fault, but not why. Where could the viewer read the fault from?
