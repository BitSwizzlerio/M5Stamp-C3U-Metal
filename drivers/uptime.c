/*
 * uptime.c - time since start-up, from the CPU cycle counter.
 *
 * The counter is only 32 bits and wraps about every 107 s at 40 MHz. Each call
 * adds the cycles since the previous call to a 64-bit total, so the total stays
 * right as long as something calls uptime_cycles() at least that often. The
 * REPL's idle loop, delay() and the Ctrl-C check do.
 */
#include "board.h"
#include "cpu.h"
#include "uptime.h"

uint64_t uptime_cycles(void)
{
    static uint32_t last;
    static uint64_t total;
    uint32_t now = cycle_count();

    total += (uint32_t)(now - last);        /* correct across one wrap of the counter */
    last = now;
    return total;
}

uint64_t uptime_us(void)
{
    return uptime_cycles() / CPU_CYCLES_PER_US;
}
