/*
 * uptime.c - time since start-up, from the chip's SYSTIMER.
 * Solution to exercise 3.
 *
 * Counter unit 0 of the SYSTIMER counts 16 000 000 times a second from reset.
 * It is clocked from the crystal whatever the CPU is doing, and it is 52 bits
 * wide, so it won't wrap for more than 8 years. Unlike the 32-bit cycle
 * counter, nothing has to read it regularly, and it doesn't depend on CPU_MHZ.
 *
 * Read first: chip/esp32c3-regs.h (the SYSTIMER registers).
 */
#include "cpu.h"
#include "esp32c3-regs.h"
#include "uptime.h"

uint64_t uptime_us(void)
{
    uint32_t lo, hi;

    REG(SYSTIMER_UNIT0_OP) = SYSTIMER_UNIT0_UPDATE;     /* take a snapshot of the counter ... */
    while (!(REG(SYSTIMER_UNIT0_OP) & SYSTIMER_UNIT0_VALUE_VALID)) {
        /* ... and wait until it is ready */
    }
    do {                                                /* read both halves, as ESP-IDF does: */
        lo = REG(SYSTIMER_UNIT0_VALUE_LO);              /* again if the low half changed */
        hi = REG(SYSTIMER_UNIT0_VALUE_HI);
    } while (lo != REG(SYSTIMER_UNIT0_VALUE_LO));
    return (((uint64_t)hi << 32) | lo) / SYSTIMER_TICKS_PER_US;
}

/* A running total of CPU cycles. The REPL still calls it, but nothing depends on it for time any more. */
uint64_t uptime_cycles(void)
{
    static uint32_t last;
    static uint64_t total;
    uint32_t now = cycle_count();

    total += (uint32_t)(now - last);
    last = now;
    return total;
}
