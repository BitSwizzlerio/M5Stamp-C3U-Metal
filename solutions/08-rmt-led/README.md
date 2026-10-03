# Solution to exercise 8: The LED, with RMT

- **Changed files:** `drivers/sk6812.c`, `drivers/sk6812.h`, `chip/esp32c3-regs.h`.
- **Removed from the build:** `drivers/sk6812.S`, by `solution.cmake`.

## What changed

- **`chip/esp32c3-regs.h`** gets the RMT registers and bits, the peripheral clock and reset registers, RMT channel 0's output signal number (51) and the `OEN_SEL` bit. The addresses and bit positions come from ESP-IDF's `rmt_reg.h`, `system_reg.h`, `gpio_sig_map.h`, `gpio_reg.h` and `esp32c3.peripherals.ld`.
- **`drivers/sk6812.c`** now does all the work:
  - **`sk6812_init()`**
    1. turns on RMT's clock and resets it;
    2. sets `SYS_CONF` to the crystal clock, divided by 1 + 0 + 0/1, with direct memory access and the clocks on;
    3. sets `CH0CONF0` to divider 2 (a 50 ns tick), one memory block, low while idle, no carrier, then `CONF_UPDATE`;
    4. makes GPIO2 an output with `gpio_output()` and connects RMT channel 0 to it;
    5. waits 1 ms and switches the LED off.
  - **`sk6812_send_grb()`**
    1. waits until the previous colour is at least 100 µs old, which the LED needs before it takes in a new one (the same wait as in `sk6812.S`);
    2. clears the flags and resets the read pointer;
    3. writes 24 symbols, most significant bit first, and a 0 end marker into `RMTMEM`;
    4. starts sending and waits for the "finished" or "error" flag, giving up after 50 ms, and notes the time.
- **`drivers/sk6812.h`**: only the comments.
- **`solution.cmake`** removes `drivers/sk6812.S` from `SOURCES`, so the timed assembly is no longer built.

`app/lua_hw.c` didn't change at all: `led()` still calls `sk6812_send_grb()`.

## Tested on the board

After `led(0, 20, 0)`:

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `hex(peek(0x60016068))` (`SYS_CONF`) | `0x87040001`: `CLK_EN`, `SCLK_ACTIVE`, crystal, `DIV_B` = 1, direct access |
| `hex(peek(0x60016010))` (`CH0CONF0`) | `0x00010240`: divider 2, one block, idle output on |
| `hex(peek(0x60016038))` (`INT_RAW`) | `0x00000001`: channel 0 finished |
| `hex(peek(0x6000455C))` (`FUNC2_OUT_SEL_CFG`) | `0x00000233`: signal 51, `OEN_SEL` |
| `hex(peek(0x60016400))`, `hex(peek(0x6001640C))`, `hex(peek(0x60016460))` | `0x00118008` (a 0 bit), `0x00098010` (a 1 bit), `0x00000000` (end) |

A test-only build checked the waveform itself, by timing every edge on GPIO2 with the cycle counter.
- The RMT output reached the pin: it read 1 with the idle level set high and 0 with it low.
- `led(0, 20, 0)` produced 48 edges, one high and one low for each of the 24 bits.
- Short highs measured about 19 cycles (target 16) and long highs about 33 (target 32). The sampling loop can only resolve about 14 cycles, and the pattern of long highs matched the bits of the colour.
- From the first edge to the last, the frame took 1173 cycles against 1166 expected: 1250 ns per bit, as the symbols ask.

The colours themselves need someone to look at the LED: `led(40, 0, 0)` red, `led(0, 40, 0)` green, `led(0, 0, 40)` blue.
