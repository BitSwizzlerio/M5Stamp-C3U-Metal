# Exercise 9: Steady pulses, with LEDC

A pin can only be high or low. But switch it on and off fast enough, and what it drives sees the average: an LED looks dimmer, a motor runs slower. This is **pulse width modulation** (PWM). The same signal sets a hobby servo's angle, by the length of each pulse, and makes a buzzer play a note, by how many pulses come each second.

`gpio.write()` in a Lua loop could do it, slowly, with the CPU doing nothing else. The ESP32-C3 has a peripheral that does it by itself: **LEDC** ("LED control"). Once it is set up, the pulses carry on while the program does something else.

**Goal:** a `pwm` table in Lua.

- `pwm.start(pin, hz, duty)` starts pulses on a pin: `hz` of them a second, each high for `duty` percent (0 to 100, fractions allowed) of the time.
- `pwm.duty(pin, duty)` changes the duty without restarting the pulses.
- `pwm.stop(pin)` stops them and leaves the pin low.

Up to 4 pins at once, each with its own frequency.

## How LEDC makes pulses

LEDC has **timers** and **channels**. A timer counts from 0 up to 2<sup>bits</sup> − 1 and starts again; each count lasts `divider` ticks of its clock. One trip round the count is one period:

> frequency = clock ÷ (divider × 2<sup>bits</sup>)

A channel watches one timer. Its output goes high when the count reaches `HPOINT`, and low again `DUTY` counts later. With `HPOINT` at 0, the pin is high for `DUTY` ÷ 2<sup>bits</sup> of each period.

Use the 40 MHz crystal as the clock. The divider has 8 fraction bits, so it can be 1.5, but it can't be less than 1. That makes a trade-off: **the higher the frequency, the fewer bits are left for the duty.**

| Use | Frequency | Bits | Divider | Duty steps |
|---|---|---|---|---|
| servo | 50 Hz | 13 | 97.66 | 8192 |
| dimming an LED | 1 kHz | 13 | 4.88 | 8192 |
| motor | 20 kHz | ? | ? | ? |

Work out the last row: the most bits that keep the divider at least 1. Then the lowest frequency you can make, with the divider at its largest (just under 1024).

## A hardware bug

The timers can count 14 bits, but on the ESP32-C3 a duty of exactly 2<sup>14</sup>, which should be high all the time, comes out low all the time. ESP-IDF's `ledc.c` has a comment about it. Use at most 13 bits, and 100% works.

## Registers

**Clock and reset**, in the SYSTEM block:

| Register | Address | What to do |
|---|---|---|
| `SYSTEM_PERIP_CLK_EN0` | `0x600C0010` | set bit 11: LEDC's clock |
| `SYSTEM_PERIP_RST_EN0` | `0x600C0018` | set bit 11, then clear it: resets LEDC |

**LEDC**, at `0x60019000`. Timer *n* (0 to 3) and channel *n* (0 to 5) each have their own registers:

| Register | Address | Fields |
|---|---|---|
| `LEDC_CONF` | `0x600190D0` | bits 1:0 the clock for every timer: 1 APB, 2 RC_FAST, 3 the crystal. Bit 31: clock for the registers. |
| `TIMERn_CONF` | `0x600190A0` + 8*n* | bits 3:0 `DUTY_RES`: the timer counts 2<sup>bits</sup> steps. Bits 21:4 the divider, times 256. Bit 23 `RST`: 1 holds the count at 0. Bit 25 `PARA_UP`: write 1 to apply the settings. |
| `CHn_CONF0` | `0x60019000` + 0x14*n* | bits 1:0 which timer to use. Bit 2 `SIG_OUT_EN`: drive the output (0 leaves it at the idle level, low). Bit 4 `PARA_UP`: write 1 to apply. |
| `CHn_HPOINT` | `0x60019004` + 0x14*n* | the count at which the output goes high |
| `CHn_DUTY` | `0x60019008` + 0x14*n* | how many counts it stays high, times 16: the register has 4 fraction bits |
| `CHn_CONF1` | `0x6001900C` + 0x14*n* | for fading. Bit 31 `DUTY_START`: write 1 to use the new duty. For no fading, also set bit 30 (`DUTY_INC`), and 1 in bits 29:20 (`DUTY_NUM`) and in bits 19:10 (`DUTY_CYCLE`). |

The order matters: write `DUTY` and `CONF1`, then `CONF0` with `PARA_UP`.

**Connecting a channel to a pin:** make the pin an output with `gpio_output()`, then write 45 + *n* to `GPIO_FUNC_OUT_SEL_CFG(pin)`, since 45 is LEDC channel 0's output signal. Also set bit 9 (`OEN_SEL`), as exercise 8 does. To give the pin back to `GPIO_OUT`, write 128 (`SIG_GPIO_OUT_IDX`) there.

## Steps

1. **Registers:** add the ones above to `chip/esp32c3-regs.h`.
2. **A new driver**, `drivers/ledc.c` and `.h`, added to `SOURCES` in `CMakeLists.txt`:
   - `ledc_init()`: clock on, reset, every timer on the crystal;
   - `ledc_start(ch, pin, hz, duty)`: choose the bits and the divider, set up timer `ch` and channel `ch`, connect the pin;
   - `ledc_set_duty(ch, duty)` and `ledc_stop(ch, pin)`.

   Choose units for the duty that need no floating point in the driver, such as hundredths of a percent.
3. **Call `ledc_init()`** from `lua_hw_open()`, before the functions are registered.
4. **The `pwm` table** in `app/lua_hw.c`, built like the `gpio` table:
   - keep a small table of which pin each channel drives;
   - check pins with `check_free_pin()`, so the LED's and the button's pins are refused;
   - read the duty with `luaL_checknumber()`, since it may have a fraction, and refuse anything outside 0 to 100;
   - when all 4 channels are busy, say so with `luaL_error()`.
5. **Help:** add lines for the three functions to `help_text` in `app/lua_sys.c`.

## Check

With nothing connected, the pin can be read while LEDC drives it. This counts the pulses in one second and how much of the time the pin is high:

```lua
function pulses(pin, ms)
  local n, high, samples, last, t = 0, 0, 0, gpio.read(pin), millis()
  while millis() - t < ms do
    local v = gpio.read(pin)
    samples = samples + 1
    high = high + v
    if v == 1 and last == 0 then n = n + 1 end
    last = v
  end
  return n, high / samples
end
```

- `pwm.start(4, 50, 50) print(pulses(4, 1000))` prints 50 and about 0.5.
- `pwm.duty(4, 25)` gives about 0.25. `pwm.duty(4, 0)` never goes high; `pwm.duty(4, 100)` never goes low.
- `hex(peek(0x600190A0))` shows timer 0's bits and divider. Check them against what you worked out.
- `pwm.start(4, 4, 50)` is refused, and so is `pwm.start(2, 1000, 50)` (the LED's pin).
- With an LED and a 330 Ω resistor from GPIO4 to ground: `pwm.start(4, 1000, 2)` is dim, `pwm.duty(4, 100)` bright. Try `for d = 0, 100 do pwm.duty(4, d) delay(20) end`.
- `python tools/selftest.py` passes.

## Going further

- **A servo** wants a pulse of 1 to 2 ms, 50 times a second: that's a duty of 5% to 10%. Add `pwm.us(pin, microseconds)`, which sets the pulse length directly. Power the servo from 5 V, not from the board's pin.
- **A tune:** `pwm.start(4, 440, 50)` is the A above middle C on a buzzer. Each semitone up multiplies the frequency by 2<sup>1/12</sup>.
- **Fading by itself:** `CHn_CONF1` can make LEDC change the duty a step at a time, with no help from the CPU. Look up `DUTY_NUM`, `DUTY_CYCLE` and `DUTY_SCALE` in the Technical Reference Manual.
- **With exercise 2:** at 160 MHz, are the frequencies still right? What would happen if the timers counted the APB clock instead of the crystal?
- **After `reset()`:** `hex(peek(0x60004564))` still shows GPIO4 connected to LEDC, because only the CPU was reset. So why has the pin stopped pulsing?
