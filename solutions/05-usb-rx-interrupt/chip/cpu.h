/*
 * cpu.h - CPU helpers written in assembly (cpu.S), for C code.
 */
#ifndef CPU_H
#define CPU_H

#include <stdint.h>

void     cycle_counter_start(void);         /* start counting CPU cycles (SystemInit() calls this) */
uint32_t cycle_count(void);                 /* CPU cycles so far; wraps about every 107 s at 40 MHz */
void     cpu_interrupts_on(void);           /* let interrupts happen (sets mstatus.MIE) */
void     cpu_interrupts_off(void);          /* hold them back (clears mstatus.MIE) */

#endif
