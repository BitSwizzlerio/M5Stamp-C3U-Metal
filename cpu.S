/*
 * cpu.S - CPU helpers that C can't express on its own.
 */
#include "esp32c3-regs.h"

        .section .text, "ax"

/* uint32_t cycle_count(void): CPU cycles counted so far (wraps about every 107 s at 40 MHz) */
        .global cycle_count
cycle_count:
        csrr a0, CSR_PCCR
        ret
