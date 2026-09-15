# Exercises

Each exercise changes C3U-Metal to do something new, and they get harder as they go. The first ones change a function or two. The later ones set up hardware that nothing in the project has touched yet.

| # | Exercise | What you'll learn | Where you'll work |
|---|---|---|---|
| 1 | [Warm-ups](01-warm-ups.md) | adding Lua functions in C, reading a crash report | `app/lua_hw.c`, `chip/trap.S` |
| 2 | [Run at 160 MHz](02-pll-160mhz.md) | clock sources, and why timing code cares about CPU speed | `chip/system_esp32c3.c`, `board/board.h` |
| 3 | [A clock that keeps itself](03-systimer-uptime.md) | using a hardware timer | `drivers/uptime.c` |
| 4 | [A timer interrupt](04-timer-interrupt.md) | interrupts, the vector table, saving registers | `chip/trap.S`, a new driver |
| 5 | [Keys by interrupt](05-usb-rx-interrupt.md) | interrupt-driven input, ring buffers | `drivers/usb_serial.c` |
| 6 | [Ctrl-C while typing](06-ctrl-c-multiline.md) | the Lua C API, how the console works | `app/repl.c` |
| 7 | [Save a script to flash](07-save-script-to-flash.md) | flash memory, ROM functions, the flash cache | a new driver, `app/main.c` |
| 8 | [The LED, with RMT](08-rmt-led.md) | a peripheral that makes waveforms by itself | `drivers/sk6812.*` |

Exercise 5 needs exercise 4. The others can be done in any order, although 3 is more interesting after 2.

## Working on an exercise

1. Make a branch, so your changes stay separate from the original: `git switch -c exercise-2`
2. Edit, build and flash: `cmake --build build -t flash`
3. Check your work. Each exercise says how. Then run `python tools/selftest.py` to make sure nothing else broke.
4. If you're stuck:
   - The [ESP32-C3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf) describes every register.
   - ESP-IDF's register headers in `components/soc/esp32c3/register/soc/` give the exact addresses and bits.
   - After that, look at the worked solution.

If a change stops the board from starting, hold its button while plugging it in, then flash a version that works.

## Worked solutions

`solutions/` has a folder for each exercise. Each folder holds complete copies of the files the solution changes or adds, in the same folders as the project, plus a README explaining it. You can build a solution without touching your own files:

```
cmake --preset default -B build-solution -DSOLUTION=02-pll-160mhz
cmake --build build-solution -t flash
```

To see exactly what a solution changed in a file:

```
git diff --no-index chip/system_esp32c3.c solutions/02-pll-160mhz/chip/system_esp32c3.c
```
