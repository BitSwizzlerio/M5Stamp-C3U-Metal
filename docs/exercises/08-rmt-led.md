# Exercise 8: The LED, with RMT

`drivers/sk6812.S` makes the LED's pulses by counting CPU cycles. It works, but only:
- at the speed its timing was measured at;
- with interrupts off while it sends;
- from RAM, so the flash cache can't pause it.

The ESP32-C3 has a peripheral made for exactly this job: **RMT** ("remote control"). It was designed for infrared remote controls and LED strips. You give it a list of *symbols*, and it plays them on a pin, with the timing done in hardware.

**Goal:** make `sk6812_send_grb()` use RMT channel 0 on GPIO2, and stop building `sk6812.S`.

## Symbols

Each symbol is 32 bits holding two (level, duration) pairs:

| Bits | Meaning |
|---|---|
| 14:0 | how long the first part lasts, in ticks |
| 15 | the first part's level |
| 30:16 | how long the second part lasts |
| 31 | the second part's level |

A duration of 0 ends the list.

One SK6812 bit is exactly one symbol: high for a while, then low. With a 50 ns tick:

| LED bit | High | Low | Symbol |
|---|---|---|---|
| 0 | 400 ns = 8 ticks | 850 ns = 17 ticks | `0x00118008` |
| 1 | 800 ns = 16 ticks | 450 ns = 9 ticks | `0x00098010` |

Work out one of those hex values yourself from the table above it, to be sure how the bits fit together.

## Registers

**Clock and reset**, in the SYSTEM block:

| Register | Address | What to do |
|---|---|---|
| `SYSTEM_PERIP_CLK_EN0` | `0x600C0010` | set bit 9: RMT's clock |
| `SYSTEM_PERIP_RST_EN0` | `0x600C0018` | set bit 9, then clear it: resets RMT |

**RMT**, at `0x60016000`:

| Register | Address | Fields |
|---|---|---|
| `RMT_SYS_CONF` | `0x60016068` | bit 0 `APB_FIFO_MASK`: 1 means you read and write the symbol memory directly. Bits 11:4 `SCLK_DIV_NUM`, 17:12 `SCLK_DIV_A`, 23:18 `SCLK_DIV_B`: the clock is divided by 1 + num + a/b. Bits 25:24 `SCLK_SEL`: 3 is the 40 MHz crystal. Bit 26 `SCLK_ACTIVE` and bit 31 `CLK_EN`: clocks on. |
| `RMT_CH0CONF0` | `0x60016010` | bit 0 `TX_START`. Bits 1 and 2 `MEM_RD_RST`, `APB_MEM_RST`: write 1 then 0 to start again from the first symbol. Bit 5 `IDLE_OUT_LV` and bit 6 `IDLE_OUT_EN`: the level while not sending. Bits 15:8 `DIV_CNT`: this channel's divider. Bits 18:16 `MEM_SIZE`: memory blocks, 48 symbols each. Bit 21 `CARRIER_EN`: keep it 0. Bit 24 `CONF_UPDATE`: write 1 to apply the settings. |
| `RMT_INT_RAW` | `0x60016038` | bit 0: channel 0 finished sending; bit 4: channel 0 error |
| `RMT_INT_CLR` | `0x60016044` | write the same bits to clear them |
| `RMTMEM` | `0x60016400` | channel 0's symbols |

**Connecting RMT to the pin:** write 51 to `GPIO_FUNC_OUT_SEL_CFG(2)`, since 51 is RMT channel 0's output signal. Also set bit 9 (`OEN_SEL`), so the output driver is still switched on by `GPIO_ENABLE`.

## Steps

1. **`sk6812_init()`:**
   - clock and reset RMT;
   - `SYS_CONF`: crystal clock, divider 1, direct memory access, clocks on;
   - `CH0CONF0`: divider 2 (40 MHz ÷ 2 = 20 MHz, a 50 ns tick), one memory block, low while idle, no carrier, `CONF_UPDATE`;
   - make the pin a GPIO output with `gpio_output()`, then connect RMT to it.
2. **`sk6812_send_grb()`:**
   - clear the flags and restart from the first symbol;
   - write the 24 symbols, most significant bit first, and an end marker;
   - set `CONF_UPDATE` and `TX_START`, and wait for the "finished" flag. If something is wrong, give up after a while rather than hang for ever.
3. **Stop building `sk6812.S`:** remove it from `SOURCES` in `CMakeLists.txt`.

## Check

- `led(40, 0, 0)` is red, `led(0, 40, 0)` green, `led(0, 0, 40)` blue.
- After a `led()` call, `hex(peek(0x60016038))` has bit 0 set.
- `hex(peek(0x60016400))` shows the first symbol you wrote.
- `python tools/selftest.py` passes.

## Going further

- Combine this with exercise 2 (160 MHz) or exercise 4 (interrupts). RMT takes its clock from the crystal, so the colours should stay right without any changes. Try it.
- 48 symbols is only two LEDs' worth. To drive a long LED strip, RMT can be refilled while it sends. Look up its "threshold" interrupt in the Technical Reference Manual.
