# Solution to exercise 13: Pulse catch

- **Changed files:** `app/plc.c`, `app/plc.h`, `app/lua_plc.c`.

## How it works

- **`app/plc.c`**:
  - `catch_mask` holds the inputs with pulse catch on, and `seen` the ones seen different from the last input image since the last scan.
  - `plc_poll()` reads `GPIO_IN` on every call while the ladder runs, before it checks whether a scan is due: `seen |= (GPIO_IN ^ inputs) & catch_mask`.
  - `scan()` takes the input image as `pins ^ caught`, where `caught = seen & ~(pins ^ inputs)`: the caught pins that changed and are back again. So the scan sees the pulse, and only that scan, because `seen` is cleared straight after.
  - At the next scan the pin reads as it really is. In between, `plc_poll()` sees the pin differ from the flipped image and sets `seen` again. But by then `pins ^ inputs` is 1 for that pin, so `caught` is 0, and nothing flips twice.
  - `plc_set_catch()` and `plc_catches()` are what Lua calls.
- **`app/lua_plc.c`**: `plc.catch(pin, on)` and `plc.catch(pin)`, taking a number or a name like `plc.pull()`, and a line in `plc.help()`.
- **`app/plc.h`** declares the two functions.

Pulse catch isn't kept in `plc_image`, so the layout and the C3U Viewer don't change.

## Tested on the board

`examples/ladder/toggle.il`, scanning every 10 ms, with C0 counting presses of G9 (`LDF G9`). Each press pulls G9 low for a set time, 40 presses of each length, 23 to 40 ms apart.

Presses made by Lua with `poke()` (the loop in the exercise), so `plc_poll()` only runs at Lua's hook:

| Press | 0.2 ms | 0.5 ms | 1 ms | 2 ms | 5 ms |
|---|---|---|---|---|---|
| pulse catch off | 1 | 1 | 6 | 10 | 18 |
| pulse catch on | 4 | 5 | 11 | 23 | 40 |

In that loop Lua's hook comes only every few milliseconds, so pulse catch can't do better than what it sees at each hook.

Presses made from outside the CPU, over JTAG (OpenOCD's `write_memory`, about 1 ms between turning G9's output on and off), while the console waits at the prompt and `plc_poll()` runs all the time:

| Press of about 1 ms | Seen |
|---|---|
| pulse catch off | 3 of 40 |
| pulse catch on | 40 of 40 |

`python tools/selftest.py`: 42 of 42.
