# Exercise 11: Measuring a voltage, with the ADC

So far a pin is 1 or 0. An **ADC** (analog-to-digital converter) turns the voltage on a pin into a number: how far a knob has been turned, how bright the room is, how flat a battery has got. The ESP32-C3 has a 12-bit ADC, so the number is 0 to 4095.

**Goal:** `adc(pin [, atten])`, which reads GPIO0, 1, 3 or 4 and returns 0 to 4095. `atten` sets the range (below), 3 if it is left out.

This is the hardest exercise: the registers are only half of it. The other half is calibration, without which every reading is wrong by a third of the range or more.

## How the ADC works

It's a **SAR** ADC ("successive approximation register"). It finds the voltage the way you would guess a number from 0 to 4095 by asking "higher or lower?": compare with half the range, then a quarter, and so on. Twelve comparisons give twelve bits.

The pins of **ADC1**, channels 0 to 4, are GPIO0 to GPIO4. GPIO2 drives the LED, so on this board that leaves 0, 1, 3 and 4.

Before the comparisons, an **attenuator** divides the voltage, so that a bigger range fits. On the board this exercise was written on, 4095 meant about:

| `atten` | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| 4095 is about | 0.8 V | 1.1 V | 1.55 V | 2.9 V |

Anything higher reads 4095. **Never put more than 3.3 V on a pin.**

## Registers

**Clock and reset**, in the SYSTEM block:

| Register | Address | What to do |
|---|---|---|
| `SYSTEM_PERIP_CLK_EN0` | `0x600C0010` | set bit 28: the ADC controller's clock |
| `SYSTEM_PERIP_RST_EN0` | `0x600C0018` | set bit 28, then clear it |

**The ADC controller**, at `0x60040000`:

| Register | Address | Fields |
|---|---|---|
| `APB_SARADC_CTRL` | `0x60040000` | bit 6 `SAR_CLK_GATED`: clock the ADC. Bits 14:7 `SAR_CLK_DIV`: set 1. Bits 28:27 `XPD_SAR_FORCE`: 3 powers the ADC on. Change only these fields. |
| `APB_SARADC_ONETIME_SAMPLE` | `0x60040020` | bit 31: use ADC1. Bit 29 `START`: 0 then 1 starts a conversion. Bits 28:25: the channel, plus 8 for ADC2. Bits 24:23: the attenuation. |
| `APB_SARADC_1_DATA_STATUS` | `0x6004002C` | bits 11:0: ADC1's result |
| `APB_SARADC_INT_RAW` | `0x60040044` | bit 31: ADC1 has finished |
| `APB_SARADC_INT_CLR` | `0x6004004C` | write bit 31 to clear it |
| `APB_SARADC_CLKM_CONF` | `0x60040054` | bits 22:21: 2 takes the clock from APB. Bit 20: clock on. Bits 7:0 a divider, and bits 13:8 and 19:14 a fraction *a*/*b* (bits 19:14 are *a*): the clock is divided by divider + 1 + *a*/*b*. Use 15, *a* = 0, *b* = 1, for 2.5 MHz. |

**The pin** must be analog: its output driver off (`GPIO_ENABLE_W1TC`), and in its `IO_MUX_GPIO` register the GPIO function with no input enable and no pull resistors.

**A conversion:** write `ONETIME_SAMPLE` with ADC1, the channel and the attenuation, and `START` 0. Clear the "finished" flag. Wait 3 µs, so the controller sees `START` change (ESP-IDF's `adc_oneshot_hal.c` explains why). Set `START`. Wait for the flag, giving up after a millisecond. Read the result, and write 0 to `ONETIME_SAMPLE`.

## Try it at the prompt first

Every one of those registers can be written with `poke()` and read with `peek()`. Before any C, get a conversion going from Lua: turn the clock on, set up the controller, and write a small `conv(channel, atten)` function that does the conversion steps. Then make GPIO4 an output with `gpio.output(4)` and read it with `gpio.write(4, 0)` and then 1. The ADC sees the pin's voltage even while the pin is an output.

You'll find 1 reads 4095, as it should. 0 does not read 0.

## Calibration

No two chips' ADCs are quite alike. Each one reads well above 0 at 0 V, by a different amount for each attenuation: around 1400 to 1700. The factory measured that amount for every chip and burned it into **eFuse**, one-time-programmable bits that can be read like memory:

| Register | Address | What is in it |
|---|---|---|
| `EFUSE_RD_SYS_PART1_DATA4` | `0x6000886C` | eFuse block 2, bits 31:0 of this pair. Bits 1:0: 1 means the calibration was burned. |
| `EFUSE_RD_SYS_PART1_DATA5` | `0x60008870` | bits 63:32 |

Counting from bit 0 of `DATA4`, the offsets for attenuations 0 to 3 are 10 bits each, starting at bits 20, 30, 40 and 50. One of them crosses from `DATA4` into `DATA5`, so put the two together in a `uint64_t`. Add 1000 to each. Compare them with what your 0 V readings were.

The ADC can take the offset off every reading itself, but the setting isn't in a memory-mapped register. It is on a small bus inside the chip that reaches its analog parts, and the ROM has a function that writes it, at `0x40001960`:

```c
void rom_i2c_writeReg_Mask(uint8_t block, uint8_t host, uint8_t reg, uint8_t msb, uint8_t lsb, uint8_t data);
```

It writes `data` into bits `msb` to `lsb` of register `reg`. Call it through a cast, as exercise 7 calls the flash functions. For the ADC, `block` is `0x69` and `host` is 0:

1. Once, open the bus to the ADC: clear bit 18 of `ANA_CONFIG` (`0x6000E044`) and set bit 16 of `ANA_CONFIG2` (`0x6000E048`). Then set register 2, bits 6:4, to 1, as ESP-IDF does.
2. Before each reading, write the offset for its attenuation: its top 4 bits into register 1, bits 3:0, and its low 8 bits into register 0, bits 7:0.

Why not just subtract the offset from the result? Try it with `atten` 3 and a pin at 3.3 V.

## Steps

1. **Registers:** add the ones above to `chip/esp32c3-regs.h`.
2. **A new driver**, `drivers/adc.c` and `.h`, added to `SOURCES` in `CMakeLists.txt`: `adc_init()` and `adc_read(pin, atten)`, which returns the reading, or −1 if the conversion never finished.
3. **Call `adc_init()`** from `lua_hw_open()`.
4. **`adc(pin [, atten])`** in `app/lua_hw.c`. Check the pin with `check_pin()`, then refuse pins above 4 and the LED's pin. Read the attenuation with `luaL_optinteger(L, 2, 3)`.
5. **Help:** add a line to `help_text` in `app/lua_sys.c`.

## Check

- `adc(5)`, `adc(2)` and `adc(4, 4)` are refused, each with its own message.
- With nothing connected, `adc(4)` gives some number; the pin floats.
- With a wire from GPIO4 to **GND**: `adc(4, a)` reads less than about 10 for every `a` from 0 to 3. Before calibration, it read the offsets.
- With a wire from GPIO4 to **3V3**: 4095.
- With a potentiometer, its ends on 3V3 and GND and its middle on GPIO4, `while true do print(adc(4)) delay(200) end` follows the knob. With `atten` 3 it should reach 4095 a little before the end.
- `python tools/selftest.py` passes, with the wire taken off GPIO4.

## Going further

- **Millivolts.** eFuse block 2 also holds what the factory read at a known voltage: 400, 550, 750 and 1370 mV for attenuations 0 to 3. They are 10 bits each from bits 60, 70, 80 and 90 (now running into `DATA6` at `0x60008874` and `DATA7` at `0x60008878`). Bit 9 is a sign: the reading is 2000 plus the low 9 bits, or minus them if bit 9 is set. Then millivolts ≈ reading × known mV ÷ factory reading. Add `adc_mv(pin)`.
- **Noise.** Read the same voltage 100 times and print the smallest and largest. Does averaging 16 readings help?
- **The two pull resistors** make a voltage divider inside the pin. With both on, what does the pin read, in millivolts, at attenuation 2 and at 3? They should agree.
- **The chip's own temperature** is measured by a sensor in the same block. ESP-IDF's `temperature_sensor_ll.h` shows how.
- **With exercise 2**, APB runs at 80 MHz, so the controller's clock doubles to 5 MHz. ESP-IDF runs it at exactly that, so it should still work. Does it?
