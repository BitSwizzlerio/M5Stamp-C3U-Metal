/*
 * uptime.h - time since start-up, from the CPU cycle counter.
 */
#ifndef UPTIME_H
#define UPTIME_H

#include <stdint.h>

uint64_t uptime_cycles(void);               /* CPU cycles since start-up (CPU_CYCLES_PER_MS in board.h) */
uint64_t uptime_us(void);                   /* microseconds since start-up, from the SYSTIMER */

#endif
