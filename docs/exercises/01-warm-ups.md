# Exercise 1: Warm-ups

Four short tasks to get used to the edit, build, flash and test cycle.

## 1a. Rainbow colours

`led(r, g, b)` takes red, green and blue. Colours are often easier to pick as **hue**, **saturation** and **value**:
- **Hue** (0 to 359) is the position round the colour wheel: 0 is red, 120 green, 240 blue.
- **Saturation** (0 to 255) is how strong the colour is.
- **Value** (0 to 255) is how bright it is.

Add a Lua function `led_hsv(h, s, v)` to `app/lua_hw.c`.

Hints:
- Start from a copy of `l_led()`. Check the arguments with `luaL_checkinteger()` and `luaL_argcheck()`, as `channel()` does.
- Register it in `lua_hw_open()` with `lua_register()`.
- One way to convert is to split the wheel into six 60-degree pieces. In each piece one channel is at full `v`, one is at the bottom, and one is rising or falling:
  ```
  bottom  = v * (255 - s) / 255
  ramp    = (v - bottom) * (h % 60) / 60
  piece 0: r = v,             g = bottom + ramp, b = bottom
  piece 1: r = v - ramp,      g = v,             b = bottom
  ...and so on round to piece 5
  ```
- Add a line for it to the help text in `app/lua_sys.c`.

**Check:** `for h = 0, 359, 3 do led_hsv(h, 255, 40) delay(20) end` fades the LED once round the rainbow.

## 1b. A pin of your own

No C this time. Connect an LED and a 330 Ω resistor in series between **GPIO4** and **GND**, with the LED's longer leg towards GPIO4. At the prompt, write a Lua function `blink(pin, times, ms)` that blinks it.

Then connect a push button between **GPIO5** and **GND**, and make the LED light while the button is held. `gpio.input(5, "up")` turns on the pull-up resistor, so the pin reads 1 until the button connects it to ground.

Remember that `gpio.read()` returns 1 or 0, and in Lua 0 counts as true. Write `if gpio.read(5) == 0 then`, not `if not gpio.read(5) then`.

**Check:** `blink(4, 3, 200)` blinks three times. Functions you type at the prompt are forgotten when the board restarts; exercise 7 fixes that.

## 1c. Read a crash report

Type `crash()`. The board prints a report and stops. Using the report:

1. `mepc`, the address of the instruction that trapped, is 0. How did the CPU end up at address 0?
2. Look up the `ra` value: `riscv32-esp-elf-addr2line -f -e build/c3u-metal.elf <ra>`. Which function made the jump?
3. Show the instructions just before `ra`:
   `riscv32-esp-elf-objdump -d build/c3u-metal.elf --start-address=<ra minus 16> --stop-address=<ra>`
   Find the `jalr`. Which register held the address it jumped to, and where did that value come from?

Unplug and replug the board. Then **extend the report to print `mstatus`**, the CPU's status register. `chip/trap.S` reads the CSRs into `a0` to `a4` before calling `trap_report()`. A sixth argument goes in `a5`.

**Check:** `crash()` now prints an `mstatus` line. On the C3U it shows `0x00001881`. Work out what that value says:
- Bits 11 and 12 (MPP, the "previous privilege mode") are both 1, so the CPU was in machine mode, the only mode this program uses.
- Bit 7 (MPIE) keeps what bit 3 (MIE, "interrupts on") was before the trap. It is 1, so the ROM left interrupts switched on. Nothing interrupts anyway, because no interrupt source is connected to the CPU. Exercise 4 connects one.

## 1d. Toggle

Blinking an LED means remembering whether it is on. Add `gpio.toggle(pin)` to the `gpio` table: it switches an output to the other level, and returns the new level, 1 or 0.

Hints:
- Start from a copy of `l_gpio_write()` in `app/lua_hw.c`, and check the pin with `check_pin()`.
- You don't need to remember the level yourself. `gpio_output()` leaves the pin's input switched on, so `gpio_read()` sees the level the pin is being driven to.
- The `gpio` functions aren't registered one by one with `lua_register()`. Add a line to `gpio_functions[]`.
- Add a line to the help text in `app/lua_sys.c`.

**Check:** `gpio.output(4) print(gpio.toggle(4), gpio.toggle(4))` prints `1 0`. With the LED from 1b, `for i = 1, 6 do gpio.toggle(4) delay(200) end` blinks it three times.

Then think about this: `gpio_read()` reads `GPIO_IN`, the level actually on the pin. The chip also has `GPIO_OUT` (`0x60004004`), the level it is trying to drive. When could the two disagree, and which should `toggle()` use?
