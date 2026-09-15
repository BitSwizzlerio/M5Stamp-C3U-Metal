/*
 * uptime.h - time since start-up, from the CPU cycle counter.
 */
#ifndef UPTIME_H
#define UPTIME_H

#include <stdint.h>

#define CPU_CYCLES_PER_MS   40000u          /* CPU at 40 MHz */
#define CPU_CYCLES_PER_US   40u

uint64_t uptime_cycles(void);               /* CPU cycles since start-up */
uint64_t uptime_us(void);                   /* microseconds since start-up */

#endif
