# Solution to exercise 10: Sensors and displays, with I²C

- **New files:** `drivers/i2c.c`, `drivers/i2c.h`.
- **Changed files:** `app/lua_hw.c`, `app/lua_sys.c`.

No registers were added: the whole bus is made with `gpio_input()`, `gpio_output()` and `gpio_read()`.

| Lua | What it does |
|---|---|
| `i2c.setup(sda, scl [, khz])` | makes two pins the bus; the clock goes no faster than `khz`, 100 if left out (10 to 400) |
| `i2c.scan()` | a table of the addresses, 0x08 to 0x77, that answer |
| `i2c.write(addr, bytes)` | sends a string of bytes; `true` if the device answered every one |
| `i2c.read(addr, count)` | `count` bytes (1 to 256) as a string, or `nil` if nothing answered |
| `i2c.writeread(addr, bytes, count)` | sends, then a repeated start and a read; a string or `nil` |

## What changed

- **`drivers/i2c.c`**:
  - `let_go()` makes a pin an input with its pull-up; `pull_low()` makes it an output, which `gpio_output()` sets low before turning the driver on, so a line is never driven high.
  - `scl_up()` lets go of SCL and waits until it is high, giving up after 1 ms, which allows for devices that stretch the clock.
  - `start()`, `stop()`, `bit_out()` and `bit_in()` each wait half a period per phase with the cycle counter. `start()` and `stop()` also check that SDA is really high, so a device holding it low is reported as a stuck bus.
  - `byte_out()` sends 8 bits and reads the ACK on the ninth clock; `byte_in()` reads 8 bits and answers ACK, or NACK after the last byte.
  - `i2c_write()` with `stop_after` false keeps the bus, so `i2c_read()` follows with a repeated start. Whatever happens, `finish()` sends a STOP if it can and lets go of both lines.
  - Three results: `I2C_OK`, `I2C_NACK` and `I2C_STUCK`.
- **`app/lua_hw.c`**: the `i2c` table. A NACK gives `false` or `nil`; a stuck bus raises a Lua error. `i2c.scan()` calls `repl_check_interrupt()` for each address.
- **`app/lua_sys.c`**: five lines of help.

### The clock is slower than asked for

Each wait is half a period, and changing a pin takes time on top: about 4.6 µs per phase, measured below. So at `khz` = 100 the clock really runs at about 52 kHz. I²C allows any speed up to a device's maximum, and waits that are never shorter than half a period can't make a phase too short for a device, so `khz` is documented as the most the clock will do. Making up the difference is a "going further" in the exercise.

## Tested on the board

With **nothing connected** to GPIO4 and GPIO5, so only the pins' own pull-ups held the lines high.

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `i2c.scan()` before `i2c.setup()` | `call i2c.setup(sda, scl) first` |
| `i2c.setup(4, 5)`, `i2c.scan()` | an empty table |
| both lines afterwards | `gpio.read(4), gpio.read(5)` gives `1 1` |
| `i2c.write(0x3C, string.char(0, 0xAF))` | `false` |
| `i2c.read(0x3C, 2)`, `i2c.writeread(0x76, string.char(0xD0), 1)` | `nil`, `nil` |
| `i2c.setup(4, 4)` | `must be a different pin from SDA` |
| `i2c.setup(18, 19)` | `GPIO18 and GPIO19 are the USB port, which the console uses` |
| `i2c.setup(4, 5, 1000)` | `must be 10-400` |
| `i2c.read(0x3C, 0)` | `must be 1-256` |
| `i2c.write(200, "x")` | `must be 0-127 (a 7-bit address)` |

A scan of 112 addresses, each a START, an address byte, the ninth clock and a STOP (12 clock periods), timed with `micros()`:

| `khz` | Scan | Per address | Ideal per address | Clock really runs at |
|---|---|---|---|---|
| 10 | 146 952 µs | 1312 µs | 1200 µs | 9.1 kHz |
| 50 | 39 033 µs | 349 µs | 240 µs | 34 kHz |
| 100 | 25 629 µs | 229 µs | 120 µs | 52 kHz |
| 400 | 15 389 µs | 137 µs | 30 µs | 87 kHz |

The difference is the same, about 110 µs per address, at every speed: 24 phases of about 4.6 µs.

**Not yet tested:** talking to a real device, and a stuck bus. Both need something wired to the pins: a sensor board, and a wire holding SDA to ground. The exercise's Check section has the steps.
