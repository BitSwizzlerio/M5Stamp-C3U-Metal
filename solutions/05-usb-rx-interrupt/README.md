# Solution to exercise 5: Keys by interrupt

This builds on the solution to exercise 4, so this folder also holds all of that solution's files.

Changed compared with exercise 4: `drivers/usb_serial.c`, `drivers/usb_serial.h`, `app/repl.c`, `app/main.c`, `chip/esp32c3-regs.h`.

## What changed

- **`chip/esp32c3-regs.h`**: `USB_SERIAL_INT_ENA`, `USB_SERIAL_INT_CLR` and the `SERIAL_OUT_RECV_PKT` bit.
- **`drivers/usb_serial.c`**:
  - `usb_serial_start_rx_interrupt()` clears and enables `SERIAL_OUT_RECV_PKT`. It then connects source 26 to CPU interrupt 4 with exercise 4's `interrupt_attach()`.
  - `rx_handler()` reads bytes while `RX_AVAIL` is set, into a 1024-byte ring buffer. It counts Ctrl-Cs in `ctrl_c_count`, and clears the interrupt flag only once the hardware is empty.
  - `usb_serial_getc()` takes bytes from the ring buffer.
  - New: `usb_serial_take_ctrl_c()` says whether a Ctrl-C has arrived since it was last called; `usb_serial_discard_input()` empties the buffer.
  - Sending hasn't changed.
- **`app/repl.c`**:
  - The typed-ahead buffer (`typed_ahead`, `keep_key()`, `next_key()`) is gone. `read_line()` calls `usb_serial_getc()`.
  - `repl_check_interrupt()` only asks `usb_serial_take_ctrl_c()`. On Ctrl-C it throws away the buffered input, the Ctrl-C included, and raises `interrupted!`.
  - `do_call()` clears the Ctrl-C flag before running a line. Otherwise a Ctrl-C pressed at the prompt, which `read_line()` already dealt with, would stop the next program straight away.
- **`app/main.c`** starts the receive interrupt after `tick_start()` and before `cpu_interrupts_on()`.

## Why the ring buffer needs no locking

The handler only ever changes `rx_head`, and the main program only ever changes `rx_tail`. Each side reads the other's index but never writes it. The worst that can happen is that `usb_serial_getc()` doesn't see a byte the handler is adding at that very moment, and it gets it on the next call. There is only one CPU core, and the handler can't be interrupted by itself, so nothing else can touch the indices.

Ctrl-C is a count rather than a flag for the same reason. A flag that the main program reads and then clears has a gap between the two: a Ctrl-C arriving in that gap would be cleared without being seen. With a count, `usb_serial_take_ctrl_c()` reads it once and remembers how many it has reported, so a Ctrl-C that lands after the read is simply reported next time.

## Tested on the board

**The paste test:** type `delay(3000)`, and while it waits, send `x = "` followed by 300 letters `a`, a closing `"` and Enter.

- **The project without this solution:** the line was cut off after about 60 characters. The string was never closed, so the console waited at `>>`.
- **This solution:** all 300 characters arrived, and `print(#x)` printed `300`.

Other checks:

| Check | Result |
|---|---|
| `python tools/selftest.py` | 15 of 15 |
| `while true do end`, then Ctrl-C | `interrupted!` |
| Ctrl-C at an empty prompt, then a `print()` | runs normally, not interrupted |
| `a = ticks() delay(1000) print(ticks() - a)` | 1002 |
