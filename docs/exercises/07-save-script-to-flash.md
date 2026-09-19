# Exercise 7: Save a script to flash

Everything you type at the prompt is forgotten when the board restarts. Make the board keep a Lua script in flash, and run it every time it starts.

**Goal:** three new Lua functions, plus a change at start-up.

- `save(code)` stores a string of Lua code in flash.
- `saved()` returns the stored code, or `nil`.
- `erase()` removes it.
- At start-up, `main.c` runs the stored code before the prompt appears. Ctrl-C must still stop it.

## Where to keep it

The flash chip holds 4 MB, and the program uses roughly the first 240 KB. Use the **last 4 KB sector**, at offset `0x3FF000`. Check it's empty first: `hex(peek(0x3C3FF000))` should print `0xffffffff`.

Give the stored data a small header, such as a magic number and the length, followed by the text. An erased sector then has no magic number, so it reads as "nothing saved".

## What you need to know about flash

- **Writing can only change 1 bits to 0.** Erasing a sector sets every bit back to 1, so every byte becomes `0xFF`. To store something: erase, then write.
- **Reading is easy.** Flash is visible through the cache, so offset `0x3FF000` can be read at address `0x3C3FF000`, like any memory.
- **Erasing and writing are not.** While they happen, the cache must be off. With the cache off, the CPU can't read *anything* from flash, including the code it is running. So the function that erases and writes must:
  - run from RAM: put it in the `.iram1` section, like `sk6812_send_grb`, using `__attribute__((section(".iram1")))`;
  - only use data that is in RAM. Copy what you'll write into a RAM buffer first, and avoid string constants, which live in flash;
  - call no functions that live in flash;
  - keep interrupts off while the cache is off, if exercise 4 is in your build: the vector table and the handlers are in flash too.
- **The ROM already has flash functions.** Call them by their addresses, which come from ESP-IDF's `components/esp_rom/esp32c3/ld/esp32c3.rom.ld`:

| Function | Address |
|---|---|
| `uint32_t Cache_Suspend_ICache(void)`: cache off; returns a value to pass to resume | `0x40000524` |
| `void Cache_Resume_ICache(uint32_t autoload)` | `0x40000528` |
| `int esp_rom_spiflash_erase_sector(uint32_t sector)`: sector number, not offset | `0x40000128` |
| `int esp_rom_spiflash_write(uint32_t offset, const uint32_t *data, int32_t len)`: whole words | `0x4000012c` |
| `int esp_rom_spiflash_config_param(uint32_t id, uint32_t chip_size, uint32_t block_size, uint32_t sector_size, uint32_t page_size, uint32_t status_mask)` | `0x40000134` |
| `void Cache_Invalidate_Addr(uint32_t address, uint32_t size)` | `0x400004d4` |

  In C, a function at a fixed address can be called through a cast, for example:
  `#define rom_erase_sector ((int (*)(uint32_t))0x40000128)`

- In direct boot, nothing has told the ROM's flash driver how big the chip is. Call `esp_rom_spiflash_config_param(0, 0x400000, 0x10000, 0x1000, 0x100, 0xFFFF)` once before the first erase.
- After writing, call `Cache_Invalidate_Addr(0x3C3FF000, 0x1000)`, so the cache doesn't keep showing the old contents.

## Suggested steps

1. A new driver, `drivers/flash_store.c` and `.h`, with save, erase and load functions. Add the `.c` file to `CMakeLists.txt`.
2. `save()`, `saved()` and `erase()` in `app/lua_sys.c`. Before saving, compile the code with `luaL_loadbufferx()`, so a script with a syntax error is never stored.
3. In `app/main.c`, after the banner, run the stored script. You need to run it the way the REPL runs a typed line, so errors get a traceback and Ctrl-C works. A small new function in `app/repl.c` can do that with `do_call()`.

## Check

- `save("booted_from_flash = true")`, unplug the board and plug it back in, then `print(booted_from_flash)` prints `true`.
- `save("oops(")` refuses, and prints the syntax error.
- `save("while true do end")` and restart: Ctrl-C still gets you to the prompt.
- `erase()` and restart: `print(saved())` prints `nil`, and `hex(peek(0x3C3FF000))` is `0xffffffff` again.
- `python tools/selftest.py` passes, with nothing saved.

If a mistake erases the wrong sector (sector 0, say), the board won't start. Hold the button while plugging it in, and flash again. Nothing is permanently harmed.

A saved script that crashes the board (`crash()`, or a `peek()` of an address with nothing behind it) crashes it again at every start-up, and flashing the program again doesn't help: the store is in a sector the program never touches. `python tools/flash.py --erase` erases the whole flash first, the store included, and then writes the program.
