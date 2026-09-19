# Solution to exercise 7: Save a script to flash

- **New files:** `drivers/flash_store.c` and `drivers/flash_store.h`.
- **Changed files:** `app/lua_sys.c`, `app/main.c`, `app/repl.c`, `app/repl.h`.

## What it adds

| Lua | What it does |
|---|---|
| `save(code)` | Checks the code compiles, then stores it in flash. Returns `true`, or `nil` and the syntax error. |
| `saved()` | The stored code, or `nil`. |
| `erase()` | Removes it. |

At start-up, `main.c` runs the stored code before the prompt appears. It runs through `repl_run_chunk()`, which uses the same error handler and Ctrl-C check as typed lines, so even a saved endless loop can be stopped.

## How the store works (`drivers/flash_store.c`)

- **Where:** the last 4 KB sector of the 4 MB flash, at offset `0x3FF000`, far from the program at the start. Its first word is a magic number, `"LUA1"`, then the length, then the text. An erased sector is all `0xFF`, so it has no magic number and reads as empty.
- **Reading** needs nothing special. The sector is visible through the data cache at `0x3C3FF000`, so `flash_store_load()` just checks the header and returns a pointer. Lua compiles straight from flash.
- **Erasing and writing** use the ESP32-C3 ROM's own flash functions, called at their addresses from ESP-IDF's `esp32c3.rom.ld`. While they run, the flash cache must be off. With the cache off, the CPU can't fetch anything from flash, so:
  - `write_sector()`, the function that turns the cache off, calls the ROM and turns it back on, is placed in `.iram1`, so it runs from RAM;
  - the data it writes is first copied into `sector[]`, which is in RAM (`.bss`);
  - it uses no string constants and calls no flash functions, only ROM addresses;
  - it turns interrupts off before suspending the cache and back on afterwards, writing `mstatus` directly. With exercise 4 in the same build, an interrupt while the cache is off would fetch the vector table from flash and crash.
- `esp_rom_spiflash_write()` writes whole 32-bit words, so the length is rounded up to a multiple of 4 and the buffer is a `uint32_t` array.
- `update()`:
  - calls `esp_rom_spiflash_config_param()` once, to tell the ROM's driver the chip size. In direct boot nothing else has.
  - calls `Cache_Invalidate_Addr()` after writing, so later reads don't see old contents the cache still holds.

## Tested on the board

```
> print(saved())
nil
> save("oops(")
nil	saved:1: unexpected symbol near <eof>
> save("booted_from_flash = true")
true
> hex(peek(0x3C3FF000))
0x3141554c                        -- "LUA1"

   (reset by flashing again)
> print(booted_from_flash)
true
> save("while true do end")
true

   (reset: the loop runs at start-up; Ctrl-C)
interrupted!
stack traceback:
	saved:1: in main chunk
> erase()
true
> hex(peek(0x3C3FF000))
0xffffffff

   (reset)
> print(booted_from_flash, saved())
nil	nil
```

`python tools/selftest.py`: 15 of 15, with nothing saved.

## If a saved script crashes the board

A saved script that traps, such as `save("crash()")`, crashes the board again at every start-up. Flashing the program again doesn't help, because the store sits in a sector the program never touches. `python tools/flash.py --erase` erases the whole flash first, the store included, and then writes the program. Tested: after `save("booted_from_flash = true")` and a reset, `flash.py --erase` left `hex(peek(0x3C3FF000))` reading `0xffffffff` and `saved()` returning `nil`.
