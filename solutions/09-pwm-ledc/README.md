# Solution to exercise 9: Steady pulses, with LEDC

- **New files:** `drivers/ledc.c`, `drivers/ledc.h`.
- **Changed files:** `chip/esp32c3-regs.h`, `app/lua_hw.c`, `app/lua_sys.c`.

| Lua | What it does |
|---|---|
| `pwm.start(pin, hz, duty)` | pulses on a pin, 5 to 40 000 a second, high for `duty` percent (0 to 100, fractions allowed) of each |
| `pwm.duty(pin, duty)` | changes the duty; the pulses carry on |
| `pwm.stop(pin)` | stops the pulses; the pin is a plain output again, low |

## What changed

- **`chip/esp32c3-regs.h`** gets the LEDC registers and bits, the peripheral clock and reset registers, LEDC channel 0's output signal number (45) and the `OEN_SEL` bit. The addresses and bit positions come from ESP-IDF's `ledc_reg.h`, `system_reg.h`, `gpio_sig_map.h` and `ledc_ll.h`.
- **`drivers/ledc.c`**:
  - **`ledc_init()`** turns LEDC's clock on, resets the block (which also stops anything left running) and puts every timer on the 40 MHz crystal.
  - **`ledc_start()`** starts at 13 bits and gives up bits until the divider is at least 1, then sets up timer `ch` (hold it at zero, release it, `PARA_UP`), sets the duty, makes the pin an output and connects channel `ch` to it.
  - **`ledc_set_duty()`** writes `HPOINT` = 0 and `DUTY` = counts × 16, then `CONF1` with `DUTY_START` and one step of no fading, then `CONF0` with the timer, `SIG_OUT_EN` and `PARA_UP`.
  - **`ledc_stop()`** turns the channel's output off and gives the pin back to `GPIO_OUT`, low.
  - Duties are in hundredths of a percent, so the driver needs no floating point.
- **`app/lua_hw.c`**: the `pwm` table. Channel *n* always uses timer *n*, so up to 4 pins can run, each at its own frequency, and there is no sharing of timers to explain. A small table records which pin each channel drives. `lua_hw_open()` calls `ledc_init()`.
- **`app/lua_sys.c`**: three lines of help.

### Why 13 bits, not 14

The first version used all 14 bits. At 1 kHz, where all 14 were in use, `pwm.duty(4, 100)` left the pin **low** all the time. That is a known hardware bug: ESP-IDF's `ledc.c` says that on the ESP32-C3, 100% duty "is not reachable when the binded timer selects the maximum duty resolution". With 13 bits, 100% works at every frequency. The cost is the lowest frequency, which went from 3 Hz to 5 Hz.

## Tested on the board

Nothing was connected to GPIO4. Its pin can be read while LEDC drives it, so a Lua loop sampled it as fast as it could (about 25 000 times a second), counting rising edges and the share of samples that were high.

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `pwm.start(4, 50, 50)`, 1 s | 50 rising edges, high 48% of the time |
| `pwm.start(4, 10, 25)`, 2 s | 20 rising edges, high 24% |
| `pwm.duty(4, 75)`, 2 s | 20 rising edges, high 74% |
| 100%, at 5, 50, 1000, 4000, 20 000 and 40 000 Hz | high in every sample |
| 0%, at the same frequencies | low in every sample |
| 50%, at the same frequencies | high 50 to 51% |
| `LEDC_CONF` | `0x80000003`: register clock on, crystal |
| `TIMER0_CONF` at 1 kHz | `0x00004E2D`: 13 bits, divider 0x4E2 / 256 = 4.88 |
| `TIMER1_CONF` at 20 kHz | `0x00001F4A`: 10 bits, divider 0x1F4 / 256 = 1.95 |
| `CH0_DUTY` at 50% | `0x00010000`: 4096 of 8192 counts, times 16 |
| `FUNC4_OUT_SEL_CFG` | `0x0000022D`: signal 45, `OEN_SEL`; `0x00000080` after `pwm.stop(4)` |
| `pwm.start(4, 4, 50)` | `bad argument #2 to 'start' (must be 5-40000)` |
| `pwm.start(4, 1000, 101)` | `bad argument #3 to 'start' (must be 0-100 (percent))` |
| `pwm.start(2, 1000, 50)` | `that pin drives the RGB LED; use led()` |
| `pwm.duty(5, 50)` with nothing on GPIO5 | `no pulses on that pin: use pwm.start() first` |
| a fifth pin while four are running | `all 4 PWM channels are in use: pwm.stop() one first` |
| `pwm.start(4, 10, 50)`, then `reset()` | GPIO4 low afterwards. `FUNC4_OUT_SEL_CFG` still reads `0x22D`, since only the CPU was reset, but `ledc_init()` reset LEDC. |

Dimming, servos and buzzers need something connected to GPIO4: try the examples in the exercise.
