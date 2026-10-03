# Solution to exercise 4: A timer interrupt

- **New files:** `chip/interrupts.c`, `chip/interrupts.h`, `drivers/tick.c`, `drivers/tick.h`.
- **Changed files:** `chip/esp32c3-regs.h`, `chip/trap.S`, `chip/cpu.S`, `chip/cpu.h`, `drivers/sk6812.S`, `app/lua_hw.c`, `app/main.c`.

## How it fits together

```
SYSTIMER comparator 0 (drivers/tick.c)
  -> interrupt matrix: source 37 to CPU interrupt 3 (chip/interrupts.c)
  -> trap_vector_table entry 3 -> interrupt_entry (chip/trap.S)
  -> interrupt_dispatch() -> tick_handler() -> ticks++
```

- **`chip/interrupts.c`** has `interrupt_attach(source, cpu_int, priority, handler)`.
  - The first time it is called, it disconnects every source in the matrix and sets the threshold to 1.
  - Then it sets the chosen CPU interrupt to level-triggered with the given priority, connects the source and enables the CPU interrupt.
  - `interrupt_dispatch()` looks up the handler from `mcause`.
  - Keeping this separate from the timer means exercise 5 can attach the USB interrupt the same way.
- **`drivers/tick.c`** programs comparator 0 in ESP-IDF's order: off, period (16000 counts = 1 ms), load, on, periodic. Then it clears and enables the SYSTIMER's interrupt and attaches `tick_handler()`. The handler clears the flag first, then counts.
- **`chip/trap.S`**: entry 0 of the table still goes to the crash handler, and entries 1 to 31 go to `interrupt_entry`.
  - `interrupt_entry` saves the 16 registers a C function may change, calls `interrupt_dispatch(mcause)`, restores them and returns with `mret`.
  - It runs on the interrupted code's stack, which is fine here because there are no threads.
- **`chip/cpu.S`** has `cpu_interrupts_on()` and `cpu_interrupts_off()`, which set and clear bit 3 of `mstatus`.
- **`app/main.c`**:
  1. switches interrupts off, because the ROM leaves them on;
  2. calls `tick_start(1000)`;
  3. then calls `cpu_interrupts_on()`.
- **`drivers/sk6812.S`** clears MIE for the 30 microseconds it sends, then restores it. The instructions for this sit outside the timed loop, so the timing doesn't change.
- **`app/lua_hw.c`** adds `ticks()`.

## Tested on the board

| Check | Result |
|---|---|
| `python tools/selftest.py` | 21 of 21 |
| `a = ticks() delay(1000) print(ticks() - a)` | 1001 |
| `sk6812_last_high` / `sk6812_last_bit` after 300 `led()` calls with ticks running | 17 / 51 cycles, as without interrupts |
| `read_registers.py` | `mtvec` = `0x42000201`; `mepc` points into the program, where the last interrupt arrived |
| board's `millis()` against the PC's clock, over 10 s | within 0.15 % |
