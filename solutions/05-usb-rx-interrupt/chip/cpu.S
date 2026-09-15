/*
 * cpu.S - CPU helpers that C can't express on its own (CSRs need special instructions).
 *
 * Read first: esp32c3-regs.h (CSR_PCER, CSR_PCMR and CSR_PCCR).
 * Try this:   t = millis() delay(1000) print(millis() - t). Each of those
 *             milliseconds is 40 000 CPU cycles counted by CSR_PCCR.
 */
#include "esp32c3-regs.h"

        .section .text, "ax"

/* void cycle_counter_start(void): make CSR_PCCR count CPU cycles (Espressif-specific CSRs) */
        .global cycle_counter_start
cycle_counter_start:
        li   t0, 1
        csrw CSR_PCER, t0                   /* count CPU cycles */
        csrw CSR_PCMR, t0                   /* start counting   */
        ret

/* uint32_t cycle_count(void): CPU cycles counted so far (wraps about every 107 s at 40 MHz) */
        .global cycle_count
cycle_count:
        csrr a0, CSR_PCCR
        ret

/* void cpu_interrupts_on(void): let interrupts through by setting MIE, bit 3 of mstatus (exercise 4) */
        .global cpu_interrupts_on
cpu_interrupts_on:
        csrsi mstatus, 8
        ret

/* void cpu_interrupts_off(void): hold interrupts back by clearing MIE */
        .global cpu_interrupts_off
cpu_interrupts_off:
        csrci mstatus, 8
        ret
