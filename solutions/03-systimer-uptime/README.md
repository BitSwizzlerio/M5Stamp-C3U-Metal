# Solution to exercise 3: A clock that keeps itself

Changed files: `chip/esp32c3-regs.h`, `chip/system_esp32c3.c`, `drivers/uptime.c`, `drivers/uptime.h`, `drivers/sk6812.c`, `app/lua_hw.c`.

## What changed

- **`chip/esp32c3-regs.h`**: the SYSTEM TIMER registers used to read counter unit 0, and `SYSTIMER_TICKS_PER_US` (16).
- **`chip/system_esp32c3.c`**: a new step 6 sets `CLK_EN` in the SYSTIMER's `CONF` register, as ESP-IDF does. The counter runs from reset without it, but the register clock is what ESP-IDF relies on.
- **`drivers/uptime.c`**: `uptime_us()` takes a snapshot of the counter (`UPDATE`, wait for `VALUE_VALID`), reads its 52 bits and divides by 16. It reads the low half twice, as ESP-IDF does. `uptime_cycles()` stays, because the REPL still calls it, but nothing takes time from it any more.
- **`app/lua_hw.c`**: `delay()` waits on `uptime_us()`.
- **`drivers/sk6812.c`**: its 1 ms wait also uses `uptime_us()`.
- **`drivers/uptime.h`**: only a comment.

`millis()`, `time()` and `gettimeofday()` already used `uptime_us()`, so they changed without being touched.

## Why it's better

- The counter is clocked from the crystal, so it keeps correct time at any CPU speed. This solution works unchanged with exercise 2's 160 MHz.
- It is 52 bits wide. At 16 000 000 counts a second it wraps after about 9 years, so nothing needs to read it regularly.

## Tested on the board

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `t = millis() delay(1000) print(millis() - t)` | 1001 |
| `hex(peek(0x60023000))` | `0xc6000000`: bit 31 (`CLK_EN`) and bit 30 (unit 0 counting) are set |
| board's `millis()` against the PC's clock, over 20 s | within 0.03 % |
