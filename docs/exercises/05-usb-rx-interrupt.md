# Exercise 5: Keys by interrupt

This exercise needs exercise 4. It uses the same interrupt entry, vector table and interrupt-matrix code.

The console reads the keyboard by **polling**: it keeps asking the USB peripheral whether a byte has arrived. While Lua code runs, the Ctrl-C check reads the port too. It keeps any other keys in a 64-key "typed-ahead" buffer (`app/repl.c`), and anything beyond that is lost.

See it for yourself on the plain project:
1. Type `delay(3000)`.
2. While it waits, paste a long line, such as `x = "` followed by 300 letters `a` and a closing `"`, and press Enter.
3. When the delay ends, most of that line is missing.

**Goal:** received bytes arrive by interrupt and go into a bigger buffer. The Ctrl-C check only has to look at a flag the interrupt handler sets.

## The USB receive interrupt

These registers are in the USB Serial/JTAG block, at `0x60043000`:

| Register | Address | What it's for |
|---|---|---|
| `INT_ENA` | `0x60043010` | bit 2, `SERIAL_OUT_RECV_PKT`: interrupt when a packet arrives from the PC |
| `INT_CLR` | `0x60043014` | write 1 to bit 2 to clear the flag |
| `EP1_CONF`, `EP1` | as used in `usb_serial.c` | `RX_AVAIL` says a byte is waiting; reading `EP1` takes it |

The interrupt source number is **26**, and it is level-triggered. Connect it to a different CPU interrupt from the timer's, for example 4.

## Steps

1. **A handler in `drivers/usb_serial.c`.** While `RX_AVAIL` is set, read a byte and put it in a ring buffer. Only after that, write bit 2 to `INT_CLR`. Emptying the hardware first means the flag doesn't come straight back.
2. **The ring buffer.**
   - It is an array plus two indices: *head*, where the handler adds bytes, and *tail*, where `usb_serial_getc()` takes them.
   - A size that is a power of two lets the indices wrap round with `&`.
   - Only the handler changes head, and only the main program changes tail, so neither ever has to stop the other. Make them `volatile`.
3. **`usb_serial_getc()`** takes from the ring buffer instead of reading the hardware.
4. **A Ctrl-C flag.** When the handler sees Ctrl-C (`0x03`), it sets a flag. Add a function that returns the flag and clears it.
5. **In `app/repl.c`:**
   - The typed-ahead buffer isn't needed any more: the ring buffer keeps keys typed while Lua runs.
   - `repl_check_interrupt()` only checks the flag. If it is set, throw away the buffered input, including the Ctrl-C, and raise the `interrupted!` error.
   - Clear the flag just before running each line. Otherwise a Ctrl-C pressed at the prompt would stop the next program as soon as it started.
6. **In `app/main.c`,** start the receive interrupt before `cpu_interrupts_on()`.

## Check

- **The paste test** from the start of this page: afterwards, `print(#x)` prints 300.
- `while true do end`, then Ctrl-C, still prints `interrupted!`.
- Ctrl-C at an empty prompt, then `print(1)`: it prints 1 and is not interrupted.
- `a = ticks() delay(1000) print(ticks() - a)` still prints about 1000.
- `python tools/selftest.py` passes.

## Going further

While it waits for a key, the CPU spins round the loop in `read_line()`. Now that keys arrive by interrupt, it could sleep instead: the RISC-V `wfi` instruction waits for the next interrupt. What else in the program would have to change? Look at how `uptime.c` measures time, and think about what the cycle counter counts.
