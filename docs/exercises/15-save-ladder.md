# Exercise 15: Save the ladder to flash

A PLC runs its program from the moment it's switched on. C3U-Metal's ladder lives only in RAM: unplug the board, and the program is gone until someone types it in again.

**Goal:** three new functions in the `plc` table, plus a change at start-up.

- `plc.save()` keeps the loaded program in flash, and whether it is running, and how often it scans.
- `plc.saved()` returns what is kept, as text, or `nil`.
- `plc.erase()` removes it.
- At start-up, `main.c` loads the saved ladder, and runs it if it was running when it was saved.

This exercise needs exercise 7: start from your own solution, or from `solutions/07-save-script-to-flash`. Its `drivers/flash_store.c` already knows how to erase and write a sector.

## Where to keep it

Exercise 7 keeps the Lua script in the last 4 KB sector, at offset `0x3FF000`. Use the sector before it, `0x3FE000`, for the ladder, so each can be saved and erased without touching the other. Check it's empty first: `hex(peek(0x3C3FE000))` should print `0xffffffff`.

Change `flash_store.c` so it can keep more than one thing: give each function a **slot** argument, and give each slot its own offset and its own magic number. Why does each slot need a different magic number, if they're in different sectors anyway? (What if a mistake writes the right data to the wrong sector?) Then change exercise 7's `save()`, `saved()` and `erase()` in `app/lua_sys.c` to use the script's slot.

## What to save

`plc_image` already keeps the IL text the program was loaded from (`source` and `source_len`), comments and all. Save that, not the instruction words. At start-up the text is assembled again, so:

- a program saved by an older version of the firmware is checked again by the new assembler, and refused with a clear message if it no longer fits;
- `plc.saved()` gives back something a person can read, and `plc.load()` can take as it is.

Whether it was running, and how often it scanned, can go in the same text, as an IL comment on the first line:

```
; plc.save(): run every 10 ms
LDN  G9      ; start
...
```

When a program loaded from flash is saved again, it already has that line. Don't let a second one pile up on top of it.

## Starting it

In `app/main.c`, after the banner and before exercise 7's saved script, call a new function in `app/lua_plc.c`, `lua_plc_start_saved()`. It loads the saved text, if there is any, assembles it with `plc_assemble()` into memory from Lua's heap (`lua_newuserdatauv()`, as `plc.load()` does), installs it, and starts it if the first line says so. Say what happened:

```
The saved ladder is running, a scan every 10 ms. plc.stop() stops it; plc.erase() removes it.
```

Why should the ladder start before the saved Lua script, and not after? What would happen if the script were `while true do ... end`?

A saved Lua script that loops forever needs Ctrl-C before anything else can be typed. Does a saved ladder that turns an output on lock you out like that?

## Check

- Load `examples/ladder/numbers.il`, `plc.run(20)`, `plc.save()`, then `reset()`. The banner is followed by "The saved ladder is running, a scan every 20 ms", and `plc.status()` agrees. Press the button five times, or type `plc.set("D0", 5)`: G4 comes on.
- `save()` a Lua script as well, and `reset()`: the ladder starts, then the script runs. Each `erase()` removes only its own.
- `plc.stop()`, `plc.save()` and `reset()`: the ladder is loaded but stopped.
- `plc.erase()` and `reset()`: no program loaded, and `hex(peek(0x3C3FE000))` is `0xffffffff` again.
- `python tools/selftest.py` passes, with nothing saved.

## Going further

- A saved ladder runs its outputs as soon as the board starts. On a real machine, is that what you want? Many PLCs start in a "stop" mode after power-up until someone presses run. Add `plc.save("stopped")`, or a rule in the program itself.
- The D registers and M bits start at 0 at every start-up. A real PLC can keep chosen ones ("retentive" memory) through a power cut. Which would you keep, and how often can you write them to flash before it wears out? (Each sector can be erased about 100 000 times.)
- `tools/flash.py --erase` erases the whole flash, the saved ladder included. Does the C3U Viewer's **Flash…** dialog say so?
