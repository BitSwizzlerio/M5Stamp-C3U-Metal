# Exercise 10: Sensors and displays, with I²C

Most small sensors and displays for hobby electronics talk **I²C**: a temperature and pressure sensor (BME280), a motion sensor (MPU6050), a little OLED screen (SSD1306), and hundreds more. Two wires carry everything, shared by every device on them, and each device has its own 7-bit address.

The ESP32-C3 has an I²C peripheral, but this exercise does the job by hand on two ordinary pins, using nothing but `drivers/gpio.c`. That way you see every step of the protocol, which is the same on every chip you will ever meet. Making a protocol in software like this is called **bit-banging**.

**Goal:** an `i2c` table in Lua.

- `i2c.setup(sda, scl [, khz])` makes two pins the bus. The clock goes no faster than `khz`, 100 if it is left out.
- `i2c.scan()` returns a table of the addresses that answer.
- `i2c.write(addr, bytes)` sends a string of bytes and returns `true` if the device answered.
- `i2c.read(addr, count)` returns `count` bytes as a string, or `nil` if nothing answered.
- `i2c.writeread(addr, bytes, count)` sends, then reads without letting go of the bus. Most sensors want this: send the number of a register, then read what is in it.

Bytes travel as Lua strings, which can hold any byte: `string.char(0xD0)` makes a one-byte string, and `string.byte(s, 1, -1)` turns one back into numbers.

## How I²C works

**Two lines, never driven high.** SDA carries data and SCL the clock. Each has a pull-up resistor to 3.3 V, and every device, including ours, can only pull a line low or let go of it. So nobody can drive a line high while someone else drives it low, which would be a short circuit. This is called **open drain**.

**Data changes while SCL is low, and is read while it is high.** We make the clock. For each bit, set SDA, then let SCL go high for half a period, then pull it low again.

**Two exceptions mark the ends of a transfer**, because they change SDA while SCL is high:

| | SDA, while SCL is high | |
|---|---|---|
| START | falls | the bus is ours |
| STOP | rises | the bus is free again; both lines are left high |

**Every byte is answered.** Bytes go most significant bit first. After the eighth bit we let go of SDA for a ninth clock, and whoever received the byte pulls SDA low to say it got it: an **ACK**. If SDA stays high, that is a **NACK**: nobody is there, or the device said no.

**A transfer** starts with START, then one byte that is the 7-bit address followed by a bit saying which way the data goes: 0 for us writing, 1 for us reading. Then the data bytes, then STOP.

| Writing 2 bytes | START | address, 0 | ACK | byte | ACK | byte | ACK | STOP |
|---|---|---|---|---|---|---|---|---|
| **who sends** | us | us | device | us | device | us | device | us |

When reading, the device sends the bytes and **we** answer each one: ACK for "more, please", and NACK after the last, so that it lets go of SDA in time for the STOP.

**A repeated start** is a START where a STOP would go. `writeread` uses it: write the register number, repeated start, read. Nothing else can use the bus in between.

**Clock stretching:** a slow device may hold SCL low after we let go of it, to make us wait. So after letting go of SCL, wait until it really is high, and give up after a while (1 ms, say) rather than wait for ever.

## Open drain with the GPIO driver

`drivers/gpio.c` already has all it takes:

- to **let go** of a line: `gpio_input(pin, GPIO_PULL_UP)`. The pin's own pull-up takes it high, unless something holds it low;
- to **pull it low**: `gpio_output(pin)`, which sets the level low before it turns the driver on, so it never drives high, not even for a moment.

The pins' pull-ups are weak, about 45 kΩ. They are enough for short wires and a slow clock. Most sensor boards carry their own, stronger pull-ups of 4.7 to 10 kΩ.

## Steps

1. **A new driver**, `drivers/i2c.c` and `.h`, added to `SOURCES` in `CMakeLists.txt`. Build it up in small pieces:
   - `wait(cycles)`, using `cycle_count()`. At 100 kHz, half a period is 5 µs, which is `CPU_CYCLES_PER_US * 5`;
   - `let_go(pin)` and `pull_low(pin)`;
   - `scl_up()`: let go of SCL and wait for it to go high, giving up after 1 ms;
   - `start()`, `stop()`, `bit_out(bit)`, `bit_in()`;
   - `byte_out(byte)`, which returns whether the device ACKed, and `byte_in(more)`;
   - `i2c_setup()`, `i2c_write()` and `i2c_read()`.

   Three results are possible: the device answered, it didn't (NACK), or a line stayed low when it should have gone high (stuck). After a NACK, still send a STOP. After anything, leave both lines let go.
2. **The `i2c` table** in `app/lua_hw.c`:
   - `i2c.setup()` checks both pins with `check_free_pin()`, refuses the same pin twice, and reads the optional speed with `luaL_optinteger(L, 3, 100)`;
   - every other function refuses to run before `i2c.setup()`;
   - `i2c.scan()` tries addresses 0x08 to 0x77 (the others are reserved) by writing no bytes to each, and calls `repl_check_interrupt()` as it goes, so Ctrl-C works;
   - a NACK gives `false` or `nil`; a stuck bus raises an error with `luaL_error()`.
3. **Help:** add lines for the five functions to `help_text` in `app/lua_sys.c`.

## Check

With nothing connected:

- `i2c.scan()` before `i2c.setup()` is refused.
- `i2c.setup(4, 5) print(#i2c.scan())` prints 0, quickly.
- `print(gpio.read(4), gpio.read(5))` afterwards prints `1 1`: both lines let go.
- `i2c.write(0x3C, "x")` is `false` and `i2c.read(0x3C, 1)` is `nil`.
- `python tools/selftest.py` passes.

With a sensor board: connect its SDA to GPIO4, SCL to GPIO5, VCC to 3V3 and GND to GND. Then `i2c.setup(4, 5)` and:

- `for _, a in ipairs(i2c.scan()) do print(hex(a)) end` lists its address;
- a BME280 at 0x76 has a chip ID: `print(hex(string.byte(i2c.writeread(0x76, string.char(0xD0), 1))))` prints `0x00000060`. A BMP280 says `0x58`;
- an MPU6050 at 0x68: `print(hex(string.byte(i2c.writeread(0x68, string.char(0x75), 1))))` prints `0x00000068`.

Then hold SDA to GND with a wire and run `i2c.scan()`: it should say the bus is stuck, not hang.

## Going further

- **Time it.** `t = micros() i2c.scan() print(micros() - t)`. Each address is 12 clock periods, so at 100 kHz the scan should take 112 × 120 µs. Why does it take longer? Could the waits make up for the time spent changing pins, without ever making half a period shorter than the device needs?
- **Real open drain.** Each pin has a register `GPIO_PINn` at `0x60004074` + 4*n*, whose bit 2 (`PAD_DRIVER`) makes the pin open drain: writing 1 lets go and writing 0 pulls low, with no switching between input and output. Try it with `poke()` first.
- **A stuck bus after a reset.** If the board resets while a device is sending a 0, the device keeps holding SDA low, waiting for clocks that never come. Recover by pulsing SCL up to 9 times until SDA goes high, then sending a STOP.
- **An OLED.** An SSD1306 at 0x3C takes commands after a 0x00 byte and pixels after a 0x40 byte. Light up some pixels from Lua.
- **With the chip's own I²C peripheral**, as exercise 8 did for the LED with RMT. The bus and the Lua functions stay the same; only the driver changes.
