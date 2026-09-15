# Worked solutions

One folder for each exercise in [docs/exercises](../docs/exercises/README.md). Try the exercise yourself before looking!

Each folder holds:
- complete copies of every file the solution changes or adds, in the same folders as the project (`app/`, `chip/`, `drivers/` and so on);
- a `README.md` that explains the solution and how it was tested.

Files that aren't in the folder come from the project unchanged.

## Building a solution

Configure a separate build folder with `SOLUTION` set to the folder's name. Your own build folder and files are left alone.

```
cmake --preset default -B build-solution -DSOLUTION=04-timer-interrupt
cmake --build build-solution -t flash
python tools/selftest.py
```

`CMakeLists.txt` takes each source file from the solution folder when the solution has a copy of it, and from the project otherwise. It also compiles any extra `.c` and `.S` files the solution adds. The solution's folders come first in the include path, so its headers are used too. A `solution.cmake` file in the folder can take project files out of the build; `08-rmt-led` uses one to drop `drivers/sk6812.S`.

Exercise 5 builds on exercise 4, so `05-usb-rx-interrupt` contains all of exercise 4's files as well as its own.

| Folder | Tested on the board |
|---|---|
| `01-warm-ups` | `led_hsv()`, and the crash report with `mstatus` |
| `02-pll-160mhz` | 160 MHz, Lua four times faster, LED timing re-measured |
| `03-systimer-uptime` | clock within 0.03 % of the PC's |
| `04-timer-interrupt` | 1000 ticks a second, LED timing unchanged |
| `05-usb-rx-interrupt` | 300 characters pasted during `delay()` all arrive |
| `06-ctrl-c-multiline` | Ctrl-C at `>>` cancels the statement |
| `07-save-script-to-flash` | a saved script runs after a reset; `erase()` removes it |
| `08-rmt-led` | RMT registers, symbols and pulse timing |

Every one passes `python tools/selftest.py`.

## Comparing

```
git diff --no-index drivers/uptime.c solutions/03-systimer-uptime/drivers/uptime.c
```
