/*
 * tick.h - a steady interrupt from the SYSTIMER that counts ticks.
 * Part of the solution to exercise 4.
 */
#ifndef TICK_H
#define TICK_H

#include <stdint.h>

void     tick_start(uint32_t per_second);   /* interrupt this many times a second */
uint32_t tick_count(void);                  /* how many ticks so far */

#endif
