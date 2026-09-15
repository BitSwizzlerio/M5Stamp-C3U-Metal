# Solution to exercise 2: Run the CPU at 160 MHz

Changed files: `board/board.h`, `chip/esp32c3-regs.h`, `chip/system_esp32c3.c`, `drivers/sk6812.S`.

## What changed

- **`board/board.h`**: `CPU_MHZ` is now 160. `CPU_CYCLES_PER_MS` and `CPU_CYCLES_PER_US` are calculated from it, so `uptime.c`, `usb_serial.c` and `delay()` needed no changes.
- **`chip/esp32c3-regs.h`**: names for the fields of `SYSTEM_SYSCLK_CONF`, and the `SYSTEM_CPU_PER_CONF` register.
- **`chip/system_esp32c3.c`**, step 4:
  1. It sets bits 1:0 of `CPU_PER_CONF` to 1, which chooses 480 MHz ÷ 3 = 160 MHz.
  2. Only then does it switch the CPU to the PLL: bits 11:10 of `SYSCLK_CONF` = 1, bits 9:0 = 0. Choosing the speed first means the CPU never runs from the PLL at a speed nobody asked for. ESP-IDF uses the same order.
  3. Both writes change only their own bits.
- **`drivers/sk6812.S`**: only the `#error` check and the comments changed.
  - `T0H`, `T1H` and `TBIT` already scale with `CPU_MHZ`.
  - `HIGH_LATENCY` and `BIT_LATENCY` count CPU cycles spent in a handful of instructions. An instruction takes the same number of cycles at any clock speed, so the same corrections still fit, as the measurements below show.

## Why the PLL needs no setting up here

ESP-IDF powers up and calibrates the PLL through an internal analog bus before switching to it. C3U-Metal can skip that because the PLL is already running at 480 MHz: the chip's USB Serial/JTAG port needs it. `SYSTEM_CPU_PER_CONF` bit 2, which reads 1, confirms the 480 MHz setting.

## Tested on the board

| Check | 40 MHz (the project) | 160 MHz (this solution) |
|---|---|---|
| `hex(peek(0x600C0058))` (`SYSCLK_CONF`) | `0x000a8000` | `0x000a8400` |
| `hex(peek(0x600C0008))` (`CPU_PER_CONF`) | | `0x0000000d` |
| `t = millis() for i = 1, 300000 do end print(millis() - t)` | 604 ms | 149 ms |
| `delay(1000)` measured with `millis()` | 1001 ms | 1000 ms |
| board's `millis()` against the PC's clock, over 20 s | | within 0.05 % |
| `sk6812_last_high` / `sk6812_last_bit` (`read_registers.py`) | 17 / 51 cycles | 66 / 200 cycles (412 ns / 1250 ns) |
| `python tools/selftest.py` | 15 of 15 | 15 of 15 |
