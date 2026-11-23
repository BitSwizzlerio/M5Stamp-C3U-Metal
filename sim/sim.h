/*
 * sim.h - the simulated board in sim/hw.c, for the PC tests and the web simulator.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

void     sim_reset(void);                       /* every register 0, nothing wired */
void     sim_settle(void);                      /* carry out W1TS/W1TC writes and work out GPIO_IN */
void     sim_set_input(int pin, int level);     /* wire a pin to 0 or 1; -1 unwires it */
uint32_t sim_pins(void);                        /* GPIO_IN: every pin's level, bit n is Gn */
uint32_t sim_outputs(void);                     /* GPIO_ENABLE: the pins that are outputs */
void     sim_advance_us(uint64_t us);           /* move the clock on */
uint64_t sim_now_us(void);

#endif
