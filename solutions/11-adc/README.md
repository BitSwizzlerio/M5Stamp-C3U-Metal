# Solution to exercise 11: Measuring a voltage, with the ADC

- **New files:** `drivers/adc.c`, `drivers/adc.h`.
- **Changed files:** `chip/esp32c3-regs.h`, `app/lua_hw.c`, `app/lua_sys.c`.

| Lua | What it does |
|---|---|
| `adc(pin [, atten])` | the voltage on GPIO0, 1, 3 or 4 as 0 to 4095; `atten` 0 to 3, default 3 |

## What changed

- **`chip/esp32c3-regs.h`** gets the ADC controller's registers, its clock and reset bits, the two analog-bus switches and the two eFuse words. The addresses and bits come from ESP-IDF's `apb_saradc_reg.h`, `system_reg.h`, `efuse_reg.h`, `regi2c_ctrl_ll.h` and `esp_efuse_table.csv`.
- **`drivers/adc.c`**:
  - **`adc_init()`** turns on and resets the controller, clocks it at 2.5 MHz from APB, powers the ADC on, opens the analog bus to it and sets its reference, all as ESP-IDF's `adc_oneshot` driver and `adc_ll.h` do.
  - **`zero_offset()`** reads this chip's offset for an attenuation from eFuse block 2: 10 bits from bit 20 + 10 × `atten`, plus 1000, or 0 if bits 1:0 say there is no calibration.
  - **`adc_read()`** writes the offset over the analog bus with the ROM's `rom_i2c_writeReg_Mask()` (called by address, as exercise 7 calls the flash functions), makes the pin analog, and does one conversion: `START` low, clear the flag, wait 3 µs, `START` high, wait for the flag (at most 1 ms), read 12 bits.
- **`app/lua_hw.c`**: `adc()`, which refuses pins above 4, the LED's pin and attenuations outside 0 to 3. `lua_hw_open()` calls `adc_init()`.
- **`app/lua_sys.c`**: one line of help.

## Tested on the board

Nothing was wired to GPIO4. To give the ADC known voltages, the pin was driven as a GPIO output: `adc(4, atten)` first, which loads that attenuation's offset, then conversions started with `poke()`, because `adc()` itself switches the pin's output off.

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `adc(2)` | `that pin drives the RGB LED; use led()` |
| `adc(5)` | `only GPIO0 to GPIO4 can be read by the ADC` |
| `adc(4, 4)` | `must be 0-3` |
| `adc(4)`, `adc(3)`, `adc(1)`, `adc(0)` with the pins floating | numbers that wander, as a floating pin should |
| 1000 calls of `adc(4)` from Lua | 86 ms, about 86 µs each |

**Calibration.** GPIO4 driven to 0 V and 3.3 V, five readings each:

| `atten` | 0 V, no offset given to the ADC | offset from eFuse | 0 V, with it | 3.3 V |
|---|---|---|---|---|
| 0 | 1388 | 1407 | 3 to 4 | 4095 |
| 1 | 1533 | 1549 | 3 to 7 | 4095 |
| 2 | 1563 | 1574 | 6 to 7 | 4095 |
| 3 | 1689 | 1699 | 2 to 3 | 4095 |

Without the offset, 0 V read within 20 counts of the eFuse value, which is what that value is for. With it, 0 V reads a few counts.

**Scale.** eFuse also holds what the factory read at known voltages. On this chip that gives 4095 as about 832 mV, 1112 mV, 1551 mV and 2909 mV for attenuations 0 to 3. With both of GPIO4's pull resistors on, making a divider inside the pin, the average of 20 readings was:

| `atten` | Reading | In millivolts |
|---|---|---|
| 2 | 3898 | 1477 |
| 3 | 2060 | 1463 |

The same voltage, measured on two ranges, agrees to 1%.

A potentiometer, a light sensor or a battery need something wired to GPIO4: try the exercise's Check section.
