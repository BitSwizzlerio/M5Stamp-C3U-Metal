# Exercise 13: Pulse catch

A scan reads every pin once, at its start ([docs/ladder.md](../ladder.md#how-short-a-press-can-be)). A press that starts and ends between two scans is never seen. With the default 10 ms scan, the ladder misses a press shorter than 10 ms some of the time, and on the board a 2 ms press was seen only 9 times in 40.

A finger is slower than that, but sensors aren't. A part passing a light gate, a pulse from a flow meter or a reed switch flicked by a magnet can all be over in a millisecond. Real PLCs have **pulse catch** (also called latching inputs) for this: the input hardware notices a change between scans and holds it until the next scan has seen it.

**Goal:** a new Lua function, `plc.catch(pin, true)`, that turns pulse catch on for one input. A pulse on that pin, however short, is then seen by the next scan, for that one scan.

This needs no other exercise.

## Where to look between scans

`plc_poll()` is called far more often than scans happen: thousands of times a second while the console waits at the prompt, and at each of Lua's hooks while Lua runs. Most calls find no scan is due and return. That is the place to look at the pins in between.

## 1. Notice a change

Keep two `uint32_t`s in `app/plc.c`, one bit for each pin:

- `catch_mask`: the inputs with pulse catch on;
- `seen`: of those, the ones that have been seen different from the last input image (`plc_image.inputs`) since the last scan.

In `plc_poll()`, after the check that the ladder is running, read `GPIO_IN` and add any caught pin that differs from the input image to `seen`. One read and two logic operations: it must stay cheap, because it runs so often.

## 2. Show it to one scan

At the start of `scan()`, where the input image is read, look at each caught pin in `seen`:

- if it's still different now, the scan sees the change anyway;
- if it's back as it was, the pulse came and went between scans. Make this scan's image show it the other way round.

Then clear `seen`. Work it out with whole words, not a loop over the pins: which bits of `seen` are pins whose value now equals the last image? XOR those into the new image.

At the next scan the pin reads as it really is again. Does `seen` need anything special then? Follow it through: what does the input image hold between those two scans, and what does `plc_poll()` compare it with?

## 3. The Lua function

Add `plc.catch(pin, on)` to `app/lua_plc.c`, taking a pin number or a name such as `"G9"`, like `plc.pull()`. With only a pin, `plc.catch(9)` returns whether pulse catch is on. Add it to `plc.help()`.

## Check

Load `examples/ladder/toggle.il`, whose counter C0 counts presses (`LDF G9`), and run it. These fake presses pull G9 low with `poke()`, as the button does, for `w` microseconds each:

```lua
function pulses(w, n)
  poke(0x60004578, 128)                       -- G9's output: plain GPIO
  local c = plc.get("C0")
  for i = 1, n do
    poke(0x6000400C, 512) poke(0x60004024, 512)  -- G9 low, output on
    local e = micros() + w while micros() < e do end
    poke(0x60004028, 512)                     -- output off: the pull-up takes it back to 1
    local g = micros() + 23000 + (i * 7919) % 17000 while micros() < g do end
  end
  return plc.get("C0") - c
end
print(pulses(5000, 40))                       -- 40 presses of 5 ms
```

- Without pulse catch, 5 ms presses are seen about half the time. With `plc.catch(9, true)`, all 40.
- Try 2 ms and 0.5 ms. Fewer are caught, even with pulse catch. Why? The loop making the presses is Lua, and while Lua runs, `plc_poll()` is only called at Lua's hook. How often is that?
- `python tools/selftest.py` passes.

A press made from outside the CPU, while the console waits at the prompt, is caught however short it is. The worked solution measured that with presses of about 1 ms made over JTAG: 3 of 40 seen without pulse catch, 40 of 40 with it.

## Going further

- Pulse catch can't tell a pulse from noise. A 1 µs glitch on a long wire is caught too, and so is every bounce of a switch's contacts. Which inputs should have it, and which shouldn't?
- Catch the change with a GPIO interrupt instead (this needs exercise 4): the GPIO peripheral can interrupt on either edge, so nothing is missed even while Lua is busy. `GPIO_PIN9_REG` sets the edge, and the interrupt matrix connects GPIO's interrupt to the CPU.
- A real PLC also has the opposite of pulse catch: an **input filter**, which ignores a change until it has lasted a few milliseconds. Add `plc.filter(pin, ms)`. Can a pin have both?
