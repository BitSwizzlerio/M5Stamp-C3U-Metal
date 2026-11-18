# Ladder logic

C3U-Metal has a small PLC (programmable logic controller) built in, next to Lua. Factories use PLCs to run machines, and they are programmed in *ladder logic*: drawings of switches and relays that look like the rungs of a ladder. Here you write the program as text, and the board draws the rungs for you.

```lua
plc.load[[
LDN  G9      ; start: the board's button
OR   G4      ; seal-in: the motor keeps itself on
ANDN G10     ; stop
OUT  G4      ; motor
LD   G4
TON  T0 2000 ; wait 2 s
OUT  G5      ; fan
]]
plc.run()
plc.show()
```

```
 0 |==[/G9 ]--+==[/G10]======( G4 )==|  G4 = 1
   |          |                      |
   |==[ G4 ]==+                      |
 1 |==[ G4 ]==[TON T0 2000]--( G5 )--|  G5 = 0
               1240 / 2000 ms
```

Press the board's button and the motor (G4) starts, then keeps itself running through the seal-in contact after you let go; the fan (G5) follows 2 seconds later. The button pulls G9 to 0 while it's held, so "held" is `LDN G9`, a normally closed contact. To stop the motor, connect G10 to 3V3 for a moment, or type `plc.stop()`.

The examples [14-ladder-motor.lua](../examples/14-ladder-motor.lua), [15-ladder-flasher.lua](../examples/15-ladder-flasher.lua) and [16-ladder-and-lua.lua](../examples/16-ladder-and-lua.lua) are ready to run with the C3U Viewer.

## Reading a rung

Power flows from the left rail to the right one. A **contact** lets it through or not; a **coil** at the right end is switched on when power reaches it.

| Drawing | IL | Passes power, or does |
|---|---|---|
| `[ G8 ]` | `LD G8`, `AND G8`, `OR G8` | while G8 is 1 (a "normally open" contact) |
| `[/G10]` | `LDN G10`, `ANDN`, `ORN` | while G10 is 0 ("normally closed") |
| `[P G8]` | `LDP G8`, `ANDP`, `ORP` | for one scan, as G8 turns on |
| `[F G9]` | `LDF G9`, `ANDF`, `ORF` | for one scan, as G9 turns off |
| `( G4 )` | `OUT G4` | G4 is 1 while power reaches it, 0 otherwise |
| `(S G4)` | `SET G4` | turns G4 on when power reaches it; it stays on |
| `(R G4)` | `RST G4` | turns G4 off (or resets a timer or counter) |
| `[TON T0 2000]` | `TON T0 2000` | on once power has reached it for 2000 ms |
| `[TOF T0 500]` | `TOF T0 500` | on with power, and for 500 ms after it goes |
| `[CTU C0 5]` | `CTU C0 5` | counts power arriving; on once the count reaches 5 |

In `plc.show()`, `==` is a wire with power in the last scan and `--` one without. Contacts side by side are **in series** (AND: both must pass power). Branches stacked and joined with `+` are **in parallel** (OR: either one will do).

## Writing IL

IL (Instruction List) is a PLC's assembly language: one instruction a line, read top to bottom. Everything after `;` is a comment. A rung is:

1. `LD` (or `LDN`, `LDP`, `LDF`) and its first contact;
2. any number of contacts joined with `AND` (in series) or `OR` (in parallel);
3. optionally timers and counters: `TON`, `TOF`, `CTU`;
4. one or more coils: `OUT`, `SET`, `RST`.

The next `LD` starts the next rung.

`OR` puts one contact in parallel with *everything before it* in the rung. For anything else, build **blocks**: an `LD` in the middle of a rung starts a new block, and `ANB` or `ORB` joins the last two blocks in series or in parallel. This rung, `(G3 and G5) or (G6 and G7)`:

```
LD  G3
AND G5
LD  G6        ; a second block
AND G7
ORB           ; join the two blocks in parallel
OUT G4
```

draws as:

```
 0 |--[ G3 ]--[ G5 ]--+--( G4 )--|
   |                  |          |
   |--[ G6 ]--[ G7 ]--+          |
```

## Operands

| Name | What it is |
|---|---|
| `G0`–`G21` | the board's pins. A pin with a coil is an **output**; a pin only read by contacts is an **input**. |
| `M0`–`M255` | relays inside the PLC, for in-between results. Lua can read and set them too. |
| `T0`–`T31` | timers. As a contact, `T0` is on when the timer is done. |
| `C0`–`C31` | counters. As a contact, `C0` is on when the count has reached its preset. |

The ladder refuses the pins that would break the board: G11 to G17 (the flash chip), G18 and G19 (USB) and G2 (the RGB LED: use `led()` from Lua). G9 is the button, which the ladder may read but not drive. It reads **0** while it's held, so "held" is `LDN G9`.

Inputs have pull-down resistors, so a switch wired from the pin to 3V3 reads 1 while it's pressed. `plc.pull(8, "up")` changes that for a switch wired to GND, which then reads 0 while it's pressed.

### Coming from another PLC

Other PLCs name their inputs and outputs differently. Here, the name is the pin printed on the board:

| Elsewhere | Here |
|---|---|
| `X0`, `I0.0`, `%IX0.0` (an input) | a `G` pin read by contacts, such as `LD G8` |
| `Y0`, `Q0.0`, `%QX0.0` (an output) | a `G` pin with a coil, such as `OUT G4` |
| `M0`, internal relays | `M0`–`M255` |
| `T0`, `C0`, `K` presets | `TON T0 2000` (ms), `CTU C0 5` |
| `XIC` (examine if closed), Mitsubishi `LD`/`AND`/`OR` | `LD`, `AND`, `OR`: the contact passes power while its bit is 1 |
| `XIO` (examine if open), Mitsubishi `LDI`/`ANI`/`ORI` | `LDN`, `ANDN`, `ORN`: the contact passes power while its bit is 0 |
| `OTE`, `OTL`, `OTU` (Allen-Bradley coils) | `OUT`, `SET`, `RST` |
| `TON`, `TOF`, `CTU` | the same names; presets in ms and counts |

The board's button is a good example of `XIO`: G9 reads 1 until the button is pressed, so "pressed" is `LDN G9`.

Each output may have only one `OUT`, to catch the classic "double coil" mistake. Use `SET`/`RST`, or combine the conditions in one rung.

## Running it from Lua

| Function | What it does |
|---|---|
| `plc.load(text)` | load an IL program. Mistakes name the line: `line 3: G9 is the button ...` |
| `plc.run([ms])` | scan every `ms` milliseconds (1–1000, default 10) |
| `plc.stop()` | stop scanning; the ladder's outputs go to 0 |
| `plc.step()` | one scan now |
| `plc.show()` | draw the rungs, with power from the last scan |
| `plc.watch([ms])` | keep redrawing them in place until Ctrl-C; the ladder keeps running |
| `plc.list()` | the instructions, each with a 1 if it passed power in the last scan |
| `plc.status()` | running or not, scan times, and how many scans started late |
| `plc.get("M5")` | read a pin or relay: 1 or 0. For `T0`, the ms so far and 1 once done; for `C0`, the count and 1 once done |
| `plc.set("M5", 1)` | set a relay, so Lua can tell the ladder things |
| `plc.pull(8, "up")` | an input's pull resistor: `"down"` (the default), `"up"` or `"none"` |
| `plc.help()` | the short version of this page |

While the ladder runs, `gpio.output()`, `gpio.input()` and `gpio.write()` refuse its pins, because the next scan would undo whatever Lua wrote. `gpio.read()` still works on every pin.

### How short a press can be

A scan sees an input only if it is still there when the scan reads `GPIO_IN`. With the default 10 ms scan, a press shorter than 10 ms can fall between two scans and be missed. On the board, 40 presses of each length, made by pulling G9 low with `poke()`, gave:

| Press | 2 ms | 5 ms | 8 ms | 10 ms | 12 ms or longer |
|---|---|---|---|---|---|
| Seen by a 10 ms scan | 9 of 40 | 19 of 40 | 31 of 40 | 39 of 40 | 40 of 40 |

A press by hand lasts tens of milliseconds, so the ladder sees every real one. Real PLCs work the same way: an input must last longer than one scan plus the input filter, and for shorter pulses they have high-speed or "pulse catch" inputs. Here, `plc.run(2)` or `plc.run(1)` scans more often, as long as Lua leaves it time to (`plc.status()` counts late scans). The scan also hides contact bounce: a bounce shorter than one scan can't show up as a second press.

The C3U Viewer's drawing is slower than the ladder: it reads the board about 10 times a second, so a quick tap that the ladder counted may never light up the contact on screen. To see what the ladder saw, count it: [toggle.il](../examples/ladder/toggle.il)'s `C0` counts every press, and `print(plc.get("C0"))` shows the count.

Ladder is good at logic that must always be running: interlocks, latches, timers. Lua is good at everything else: printing, arithmetic, the RGB LED. They share the `M` relays, so each can do its part. See [16-ladder-and-lua.lua](../examples/16-ladder-and-lua.lua).

## How it works

The engine is four files in `app/`:

- **`plc_asm.c`**: the assembler. It turns IL into 32-bit instruction words, and checks everything it can (the operands, the pins, the shape of each rung), so that the scan never has to.
- **`plc.c`**: the scan. Each scan reads every pin at once with one read of `GPIO_IN`, works through the instructions, and then sets every output at once with one write each to `GPIO_OUT_W1TS` and `GPIO_OUT_W1TC`. Inside, it is a little stack machine: a contact ANDs or ORs its value into the power so far, and `LD` in the middle of a rung pushes that power onto a stack for `ANB` or `ORB` to pop.
- **`plc_show.c`**: the drawing. It turns each rung back into a tree of series and parallel groups (`AND` and `ANB` are series, `OR` and `ORB` parallel) and draws the tree into a grid of characters.
- **`lua_plc.c`**: the `plc` table.

There are no interrupts in C3U-Metal, so nothing interrupts Lua to run a scan. Instead the places that already check for Ctrl-C call `plc_poll()`: the prompt while it waits for a key, Lua's hook every 1000 instructions, and `delay()`. A scan runs whenever one is due. A long C function, or Lua's garbage collector, can make a scan late. `plc.status()` counts late scans, and a late scan is never caught up with a burst of scans.

Everything the ladder knows is in one struct, `plc_image`, at the fixed address `0x3FCBE000` (see [memory-map.md](memory-map.md)). `hex(peek(0x3FCBE000))` shows its magic number, `0x5244414c` ("LADR"). It is at a fixed address so the C3U Viewer can find it over JTAG.

### Try this

- Load a program, run it, and compare `plc.list()` with `plc.show()`. Which instruction made each piece of the drawing?
- Watch `plc.status()` while Lua runs `collectgarbage()` in a loop. Do any scans start late?
- Exercise 4 adds a timer interrupt. Could the scan run from that instead of `plc_poll()`? What would have to change, and what could go wrong if the scan ran halfway through `plc.load()`?
