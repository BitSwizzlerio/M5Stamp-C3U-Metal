# Exercise 3: A clock that keeps itself

`millis()` and `delay()` get their time from `drivers/uptime.c`, which counts CPU cycles. That works, but it has weak spots:

- The cycle counter is only 32 bits. At 40 MHz it wraps every 107 seconds, so `uptime_cycles()` must be called more often than that or time is lost. The console's idle loop and the Ctrl-C check happen to call it often enough.
- It depends on the CPU's speed. Change `CPU_MHZ` (exercise 2) and every conversion changes with it.

The ESP32-C3 has a better clock built in: the **SYSTIMER**. Its counter unit 0:
- starts counting at reset;
- counts 16 000 000 times a second, from the crystal, whatever the CPU is doing;
- is 52 bits wide, so it won't wrap for more than 8 years.

**Goal:** make `uptime_us()` read the SYSTIMER, and make `delay()` use `uptime_us()`.

## The registers

The SYSTIMER block starts at `0x60023000`.

| Register | Address | What it's for |
|---|---|---|
| `CONF` | `0x60023000` | Bit 31, `CLK_EN`, is the clock for the registers; ESP-IDF always sets it, so set it too. Bit 30 means unit 0 is counting; it is already 1 after reset. |
| `UNIT0_OP` | `0x60023004` | Write bit 30 (`UPDATE`) to take a snapshot of the counter. Bit 29 (`VALUE_VALID`) reads 1 once the snapshot is ready. |
| `UNIT0_VALUE_HI` | `0x60023040` | The snapshot's top 20 bits. |
| `UNIT0_VALUE_LO` | `0x60023044` | The snapshot's bottom 32 bits. |

To read the counter:
1. Write `UPDATE`.
2. Wait for `VALUE_VALID`.
3. Read `LO` and `HI`. ESP-IDF reads `LO` again afterwards and starts over if it has changed, so do the same.

The count is `(HI << 32) | LO`. Since there are 16 counts per microsecond, microseconds are that divided by 16.

## What to change

1. Add the registers to `chip/esp32c3-regs.h`, next to the others.
2. Set `CLK_EN` once during start-up. `SystemInit()` is a good place: it's chip setup, and it uses no global variables.
3. In `drivers/uptime.c`, make `uptime_us()` read the SYSTIMER. Keep `uptime_cycles()`: other code still calls it.
4. In `app/lua_hw.c`, make `delay()` wait on `uptime_us()` instead of `uptime_cycles()`. `drivers/sk6812.c` has a 1 ms wait you can change the same way.

`sk6812.S` keeps using the cycle counter directly for its pulses. That's fine: it needs counts in CPU cycles, not time.

## Check

- `t = millis() delay(1000) print(millis() - t)` still prints about 1000.
- `hex(peek(0x60023000))` has bit 31 set, for example `0xc6000000`.
- Compare with a real clock:
  - Type `print(millis())`.
  - Wait exactly one minute by a clock or your phone.
  - Type `print(millis())` again. The difference should be about 60000. The crystal is accurate to a few parts per million, so any error is your reaction time.
- `python tools/selftest.py` passes.

## Going further

- If you did exercise 2, run at 160 MHz. `millis()` stays right without any change to `uptime.c`.
- The SYSTIMER can also raise an interrupt when its count reaches a chosen value. That's exercise 4.
