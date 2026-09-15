# Exercise 2: Run the CPU at 160 MHz

C3U-Metal runs the CPU straight from the board's 40 MHz crystal. The ESP32-C3 can run four times as fast by taking its clock from the PLL instead.

## Where the CPU's clock comes from

- The **crystal** on the board makes a very steady 40 MHz.
- The **PLL** (phase-locked loop) multiplies that up to 480 MHz. It is already running when the program starts, because the USB port needs it.
- Two registers choose the CPU's clock:

| Register | Address | Fields |
|---|---|---|
| `SYSTEM_SYSCLK_CONF` | `0x600C0058` | Bits 11:10 are the source: 0 the crystal, 1 the PLL. Bits 9:0 divide the crystal (value + 1). |
| `SYSTEM_CPU_PER_CONF` | `0x600C0008` | Bits 1:0 set the speed when running from the PLL: 0 for 80 MHz, 1 for 160 MHz. Bit 2 says the PLL runs at 480 MHz, which it does by default. Leave bit 2 and the bits above it alone. |

After reset the CPU runs from the crystal divided by 2, which is 20 MHz. `SystemInit()` clears bits 11:0 of `SYSCLK_CONF`, which selects the crystal divided by 1.

## What to change

1. `board/board.h`: set `CPU_MHZ` to 160.
2. `chip/system_esp32c3.c`, step 4:
   - First choose 160 MHz: bits 1:0 of `CPU_PER_CONF` = 1.
   - Then switch to the PLL: bits 11:10 of `SYSCLK_CONF` = 1 and bits 9:0 = 0.
   - Change only those bits: read the register, change the bits, write it back.
3. Build. The `#error` lines stop the build in each file that assumed 40 MHz. For each one, work out what needs to change, not just the number.
   - `drivers/sk6812.S`: `T0H`, `T1H` and `TBIT` are calculated from `CPU_MHZ`. What about `HIGH_LATENCY` and `BIT_LATENCY`? They count the CPU cycles spent in a few instructions. Does an instruction take more cycles when the clock is faster? Measure it:
     ```
     python tools/read_registers.py sk6812_last_high sk6812_last_bit
     ```
     At 160 MHz the targets are 64 cycles high and 200 cycles per bit.
4. Everything else that counts cycles (`uptime.c`, `usb_serial.c`, `delay()`) gets its numbers from `board.h`, so it needs no changes. Check that this is really true.

## Check

- `hex(peek(0x600C0058))` has bits 11:10 = 01, for example `0x000a8400`. `hex(peek(0x600C0008))` has its low two bits = 01; the C3U shows `0x0000000d`.
- **Speed**: `t = millis() for i = 1, 300000 do end print(millis() - t)` takes about 600 ms at 40 MHz and about 150 ms at 160 MHz.
- **Time is still right**: `delay(1000)` still lasts 1000 ms. For a real test, compare `print(millis())` with a clock over a minute.
- **LED timing**: `read_registers.py` shows about 66 cycles high and 200 per bit.
- `python tools/selftest.py` passes.

## Going further

- Try 80 MHz. What changes?
- At 160 MHz the 32-bit cycle counter wraps every 27 seconds instead of every 107. What in `uptime.c` depends on being called more often than that? Exercise 3 removes the problem.
