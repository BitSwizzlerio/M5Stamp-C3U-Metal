/*
 * interrupts.c - connect a peripheral's interrupt to a C function.
 * Part of the solution to exercise 4; see interrupts.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include "esp32c3-regs.h"
#include "interrupts.h"

#define SOURCE_COUNT    62                  /* interrupt sources on the ESP32-C3 */

static void (*handlers[32])(void);          /* one per CPU interrupt */

void interrupt_attach(int source, int cpu_int, int priority, void (*handler)(void))
{
    static bool matrix_cleared;

    if (!matrix_cleared) {
        /* The ROM leaves some sources connected to CPU interrupts. Disconnect
           them all first, as ESP-IDF does (0 means "not connected"). */
        for (int n = 0; n < SOURCE_COUNT; n++)
            REG(INTMTX_CORE0_MAP(n)) = 0;
        REG(INTMTX_CPU_INT_THRESH) = 1;     /* ignore priority 0 */
        matrix_cleared = true;
    }

    handlers[cpu_int] = handler;
    REG(INTMTX_CPU_INT_TYPE) &= ~(1u << cpu_int);           /* level: active while the flag is set */
    REG(INTMTX_CPU_INT_PRI(cpu_int)) = (uint32_t)priority;
    REG(INTMTX_CORE0_MAP(source)) = (uint32_t)cpu_int;
    REG(INTMTX_CPU_INT_ENABLE) |= 1u << cpu_int;
}

/* Called by interrupt_entry (chip/trap.S). mcause's low 5 bits are the CPU interrupt number. */
void interrupt_dispatch(uint32_t mcause)
{
    unsigned n = mcause & 0x1F;

    if (handlers[n] != NULL)
        handlers[n]();
}
