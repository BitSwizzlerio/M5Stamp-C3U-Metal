# Example Lua scripts

Sixteen short Lua programs to run on a C3U-Metal board with the
[C3U Viewer](../viewer/README.md). Each one does something you can watch in the
viewer's pin map and History while it runs. They go roughly from easiest to hardest.
The last three use ladder logic: see [Ladder logic](#ladder-logic) below.

| File | What it does | Wiring |
|---|---|---|
| [01-hello.lua](01-hello.lua) | Prints a greeting and shows red, green and blue on the RGB LED. | none |
| [02-blink.lua](02-blink.lua) | Blinks GPIO4 and the RGB LED, twice a second. | optional LED on GPIO4 |
| [03-pull-resistors.lua](03-pull-resistors.lua) | Reads an unconnected pin with a pull-up, a pull-down and neither. | none |
| [04-button.lua](04-button.lua) | Lights GPIO4 and the LED while the button is held. | optional LED on GPIO4 |
| [05-binary-counter.lua](05-binary-counter.lua) | Counts from 0 to 15 in binary on GPIO4–GPIO7. | optional LEDs on GPIO4–7 |
| [06-traffic-light.lua](06-traffic-light.lua) | Runs a UK traffic-light sequence on GPIO4, GPIO5 and GPIO6. | optional red, amber and green LEDs |
| [07-chaser.lua](07-chaser.lua) | Runs a light up and down the nine pins on the left edge of the board. | optional LEDs |
| [08-morse.lua](08-morse.lua) | Sends a message in Morse code on GPIO4 and the LED. | optional LED on GPIO4 |
| [09-dice.lua](09-dice.lua) | Rolls a dice when you press the button, and shows the number in binary. | optional LEDs on GPIO4–6 |
| [10-reaction-timer.lua](10-reaction-timer.lua) | Times how fast you can press the button when the LED turns green. | none |
| [11-registers.lua](11-registers.lua) | Blinks GPIO4 by writing the chip's GPIO registers with `poke()`. | optional LED on GPIO4 |
| [12-too-fast-to-see.lua](12-too-fast-to-see.lua) | Blinks GPIO4 faster and faster, to show what the viewer can't keep up with. | optional LED on GPIO4 |
| [13-demux.lua](13-demux.lua) | A 1-of-4 demultiplexer: GPIO8 and GPIO10 choose which of GPIO4–GPIO7 is on. | optional switches on GPIO8 and GPIO10, LEDs on GPIO4–7 |
| [14-ladder-motor.lua](14-ladder-motor.lua) | Ladder logic: the button latches a motor on G4 through a seal-in contact, Stop drops it, and a fan on G5 follows 2 s later. | none to start; for Stop, a wire from G10 to 3V3; optional LEDs on G4 and G5 |
| [15-ladder-flasher.lua](15-ladder-flasher.lua) | Ladder logic: two timers flash G4 and G5 in turn, like a level crossing. | optional LEDs on G4 and G5 |
| [16-ladder-and-lua.lua](16-ladder-and-lua.lua) | Ladder logic turns the button into an on/off switch; Lua lights the RGB LED and prints the count. | none |

Every example works with nothing connected: the pins show up in the viewer either way.
Each file starts with a comment that says what to look for, and ends with an idea to
try.

## Running one

1. Plug in the board and start the C3U Viewer.
2. Click **Run a .lua file…** and pick an example.
3. Watch the pin map, the History and the console.
4. Most examples run until you stop them: click **Stop**, or press Ctrl-C in the console.

The viewer types the file into the console one line at a time, just as if you had typed
it yourself. Anything the file defines is still there afterwards, so once a script stops
you can carry on from the prompt: in `08-morse.lua`, for example, try `send("hi")`.

## Wiring an LED

An LED needs a resistor in series, or it can burn out the LED or the pin. Connect:

    GPIOn ── 330 Ω resistor ── LED long leg (+)    LED short leg (−) ── GND

Any resistor from 220 Ω to 1 kΩ works; a bigger one makes the LED dimmer.

## Writing your own

A file the viewer runs has to follow a few rules, because it arrives one line at a time:

- **Don't use `local` at the top level of the file.** Each line is run on its own, so a
  `local` variable is gone by the next line. Use globals, or put `local` inside a
  `function` or a `do ... end` block, where it works as normal.
- **An endless loop must be the last thing in the file.** The viewer waits for each line
  to finish before typing the next one, so nothing after `while true do ... end` would
  ever run.
- **Use `--` comments, not `--[[ ... ]]` blocks.**
- **Use pins 0, 1, 3–8, 10, 20 and 21.** GPIO2 drives the RGB LED (use `led()`) and
  GPIO9 is the button (use `button()`); `gpio.output()` and `gpio.input()` refuse both.
- **Only the functions in `help()` are there.** `pwm`, `adc` and `i2c` come from the
  exercises, so they only exist on a board flashed with one of those solutions.
- **Toggling a pin faster than about 10 times a second won't show properly in the
  viewer,** which looks at the pins about 20 times a second. `12-too-fast-to-see.lua`
  shows why.

## Ladder logic

Examples 14 to 16 use the board's ladder logic engine: a small PLC (programmable logic
controller) that runs beside Lua. The program between `plc.load[[` and `]]` is written in
IL (Instruction List), one instruction a line, using the board's own pin names: a pin
with a coil (`OUT G4`) is an output, and a pin only read by contacts (`LD G8`) is an input.
Lua loads it and starts it, and the ladder then keeps scanning by itself, even while Lua
runs something else.

- `plc.watch()` draws the rungs in the console and keeps redrawing them. `==` is a wire
  with power and `--` one without. **Stop** (or Ctrl-C) stops the drawing, not the ladder.
- `plc.stop()` stops the ladder and turns its outputs off. Until then, `gpio.write()`
  refuses the ladder's pins.
- Ladder inputs have pull-down resistors, so wire switches to 3V3: an open switch reads 0
  and a pressed one 1. The board's button, G9, is the other way round: it reads 0 while
  it's held, so "held" is `LDN G9`.
- `plc.help()` lists every instruction, and [docs/ladder.md](../docs/ladder.md) explains them.

The [ladder](ladder) folder has the same programs as plain IL files, each with an operator
panel described in its `@` comment lines: `motor.il` (which adds Start and Stop buttons on
the panel, so it needs no wiring), `flasher.il` and `toggle.il`. Two more: `numbers.il`
counts presses of the button in the register D0 and lights G4 from 5, and `tank.il` is a
simulated water tank built around its panel: a level gauge, a setpoint, and Auto, Drain and
Fill now controls. In the C3U Viewer, click **Ladder Logic**, then **Open .il file…**: the
card draws the rungs with the power flowing, and the **Operator panel** card below runs the
machine.
