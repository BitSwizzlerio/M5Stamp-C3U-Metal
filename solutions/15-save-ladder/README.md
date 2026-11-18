# Solution to exercise 15: Save the ladder to flash

This solution builds on exercise 7, so it contains all of `07-save-script-to-flash`'s files as well as its own.

- **Changed files:** `drivers/flash_store.c`, `drivers/flash_store.h`, `app/lua_plc.c`, `app/lua_plc.h`, `app/lua_sys.c`, `app/main.c`.
- **From exercise 7:** `app/repl.c`, `app/repl.h`.

## How it works

- **`drivers/flash_store.c`** keeps a table of slots, each with its own sector and magic number:

  | Slot | Offset | Magic | Kept by |
  |---|---|---|---|
  | `FLASH_SLOT_SCRIPT` | `0x3FF000`, the last sector | `"LUA1"` | `save()` (exercise 7) |
  | `FLASH_SLOT_LADDER` | `0x3FE000`, the one before | `"LDR1"` | `plc.save()` |

  `write_sector()`, which runs from RAM, now takes the offset as an argument. Every other function takes the slot.
- **`app/lua_sys.c`**: `save()`, `saved()` and `erase()` use `FLASH_SLOT_SCRIPT`.
- **`app/lua_plc.c`**:
  - `plc.save()` stores the program's IL text from `plc_image`, after a first line that says how to start it: `; plc.save(): run every 10 ms` or `; plc.save(): stopped`. That line is an IL comment, so the saved text loads as it is. A header from an earlier save is taken off first, so they don't pile up.
  - `plc.saved()` returns the text, and `plc.erase()` erases the slot.
  - `lua_plc_start_saved()` assembles the saved text into memory from Lua's heap, installs it, and runs it if the first line says so. If it doesn't assemble (say, after the firmware changed), it prints the error and leaves the board with no program.
- **`app/main.c`** calls `lua_plc_start_saved()` after the banner and before exercise 7's saved script. The ladder runs beside Lua, so a script that never ends doesn't stop it.

## Tested on the board

| Check | Result |
|---|---|
| before: `hex(peek(0x3C3FE000))`, `hex(peek(0x3C3FF000))` | `0xffffffff`, `0xffffffff` |
| `numbers.il`, `plc.run(20)`, `plc.save()`, a Lua script `save()`d too, `reset()` | "The saved ladder is running, a scan every 20 ms", then the script ran and saw the ladder running |
| after that reset: `plc.set("D0", 6)` | G4 on: the saved program works |
| `erase()`, `plc.stop()`, `plc.save()`, `reset()` | "The saved ladder is loaded, stopped"; no script |
| `plc.erase()`, `reset()` | no program loaded; both sectors `0xffffffff` again |
| `python tools/selftest.py`, nothing saved | 44 of 44 |

Not tested: a saved ladder that no longer assembles. It would need a firmware whose assembler refuses something an older one took.
