# Solution to exercise 1: Warm-ups

Changed files:
- 1a and 1d: `app/lua_hw.c`, `app/lua_sys.c`
- 1c: `chip/trap.S`, `app/trap_report.c`

## 1a. led_hsv(h, s, v)

`l_led_hsv()` in `app/lua_hw.c` checks its arguments, then converts the colour:

- `bottom = v * (255 - s) / 255` is the lowest a channel goes. Full saturation makes it 0; no saturation makes it `v`, which gives grey.
- `ramp = (v - bottom) * (h % 60) / 60` is how far through the current 60-degree piece the hue is.
- `h / 60` picks the piece:
  - one channel is at `v`;
  - one is at `bottom`;
  - the third rises from `bottom` to `v`, or falls from `v` to `bottom`.

  From red (0) the wheel goes through yellow, green, cyan, blue and magenta, and back to red.

It is registered as `led_hsv` in `lua_hw_open()`, and `help()` in `app/lua_sys.c` lists it.

Tested on the board:
- `for h = 0, 359, 3 do led_hsv(h, 255, 40) delay(5) end` runs without errors.
- `led_hsv(360, 255, 40)` gives `bad argument #1 to 'led_hsv' (must be 0-359)`.
- `python tools/selftest.py` passes 21 of 21.

## 1b. A pin of your own

No C is needed. One way to do it:

```lua
function blink(pin, times, ms)
  gpio.output(pin)
  for i = 1, times do
    gpio.write(pin, 1) delay(ms)
    gpio.write(pin, 0) delay(ms)
  end
end

blink(4, 3, 200)

gpio.output(4)
gpio.input(5, "up")
while true do                          -- Ctrl-C to stop
  gpio.write(4, gpio.read(5) == 0)     -- a pressed button pulls GPIO5 to 0
  delay(10)
end
```

`gpio.read(5) == 0` is `true` or `false`, and `gpio.write()` accepts both. That avoids the trap that 0 counts as true in Lua. This part needs an LED and a button wired to the board, so it wasn't tested here.

## 1c. Reading the crash report

1. **How did the CPU get to address 0?** `crash()` calls a function through a pointer that holds 0 (`NULL`). The CPU jumps to address 0. The four bytes there are all zero, and RISC-V defines an all-zero instruction as illegal. So the trap is "illegal instruction", and `mepc` is 0.
2. **Who jumped there?** `jalr` saves the address of the following instruction in `ra` before it jumps. So `riscv32-esp-elf-addr2line -f -e build/c3u-metal.elf <ra>` names `l_crash` in `app/lua_sys.c`.
3. **Which register?** The disassembly around `ra`, from one build (your addresses will differ):

   ```
   420011ba <l_crash>:
   420011ba:  1101   addi  sp,sp,-32
   420011bc:  ce06   sw    ra,28(sp)
   420011be:  c602   sw    zero,12(sp)    # nowhere = NULL: stored on the stack, because it is volatile
   420011c0:  47b2   lw    a5,12(sp)      # load it back into a5
   420011c2:  9782   jalr  a5             # jump to the address in a5, which is 0; ra = 0x420011c4
   420011c4:  4501   li    a0,0           # where it would have come back to
   ```

   The jump went through **a5**, which was loaded from the stack slot holding `nowhere`.

### Printing mstatus

- `chip/trap.S` also reads `mstatus` into `a5` before calling `trap_report()`.
- `trap_report()` takes it as a sixth parameter and prints it.
- The labels are one character wider, so the columns still line up.

On the board:

```
*** CPU trap: illegal instruction: the bytes at mepc are not an instruction ***
mcause   0x00000002   the reason, as a number
mepc     0x00000000   address of the instruction that trapped
mtval    0x00000000   the bad address or instruction (0 if none)
ra       0x420012e2   return address: usually where that function was called from
sp       0x3fccfd10   stack pointer
mstatus  0x00001881   CPU status: bits 12:11 = 3 means it was in machine mode
```

What `0x00001881` says:
- **Bits 12:11 (MPP) = 3:** the CPU was in machine mode.
- **Bit 7 (MPIE) = 1:** when the trap happened, the CPU saved the old value of bit 3, the interrupt enable (MIE), here. So interrupts were switched on. The ROM leaves them on, and nothing interrupts only because no interrupt source is connected to the CPU. Exercise 4 has to deal with this.
- **Bit 0** is set too. The current RISC-V specification doesn't define it.

## 1d. gpio.toggle(pin)

`l_gpio_toggle()` in `app/lua_hw.c` checks the pin with `check_pin()`, reads its level with `gpio_read()`, drives the other level with `gpio_write()`, and returns the new level. It needs no memory of its own: `gpio_output()` leaves the pin's input on, so `gpio_read()` sees what the pin is being driven to. It is one more line in `gpio_functions[]`, and `help()` lists it.

`GPIO_IN` is the level actually on the pin, while `GPIO_OUT` (`0x60004004`) is the level the chip is trying to drive. They disagree when something stronger holds the pin, such as a wire to ground, or when the pin isn't an output at all. Reading `GPIO_IN`, as this does, means toggle drives the pin to the opposite of what it really is; reading `GPIO_OUT` would flip what the chip intends, whatever the pin does. For a pin that nothing else drives, they are the same.

Tested on the board, with nothing connected to GPIO4:
- `gpio.output(4) print(gpio.toggle(4), gpio.toggle(4), gpio.toggle(4))` prints `1 0 1`, and `gpio.read(4)` is then 1.
- Six toggles leave the pin where it started.
- `gpio.toggle(18)` is refused: `GPIO18 and GPIO19 are the USB port, which the console uses`.
- `python tools/selftest.py` passes 21 of 21.
