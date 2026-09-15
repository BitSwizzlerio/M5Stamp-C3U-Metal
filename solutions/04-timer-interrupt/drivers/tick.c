/*
 * tick.c - a steady interrupt from the SYSTIMER that counts ticks.
 * Part of the solution to exercise 4.
 *
 * The SYSTIMER's counter unit 0 counts 16 000 000 times a second from reset.
 * Comparator 0 is set to "periodic": every 'period' counts it sets its
 * interrupt flag. The interrupt matrix sends that to CPU interrupt
 * TICK_CPU_INT, and tick_handler() runs.
 */
#include "esp32c3-regs.h"
#include "interrupts.h"
#include "tick.h"

#define TICK_CPU_INT    3                   /* any free CPU interrupt */

static volatile uint32_t ticks;             /* volatile: an interrupt handler changes it */

static void tick_handler(void)
{
    /* Clear the flag first. The interrupt is level-triggered, so while the flag
       stays set the CPU would jump straight back here after returning. */
    REG(SYSTIMER_INT_CLR) = SYSTIMER_TARGET0_INT;
    ticks++;
}

void tick_start(uint32_t per_second)
{
    /* Comparator 0, in the same order as ESP-IDF: off, period, load, on, periodic. */
    REG(SYSTIMER_CONF) = (REG(SYSTIMER_CONF) | SYSTIMER_CLK_EN) & ~SYSTIMER_TARGET0_WORK_EN;
    REG(SYSTIMER_TARGET0_CONF) = SYSTIMER_TICKS_PER_S / per_second;     /* counter unit 0 */
    REG(SYSTIMER_COMP0_LOAD) = 1;
    REG(SYSTIMER_CONF) |= SYSTIMER_TARGET0_WORK_EN;
    REG(SYSTIMER_TARGET0_CONF) |= SYSTIMER_TARGET0_PERIOD_MODE;

    REG(SYSTIMER_INT_CLR) = SYSTIMER_TARGET0_INT;       /* forget anything from before */
    REG(SYSTIMER_INT_ENA) |= SYSTIMER_TARGET0_INT;
    interrupt_attach(INT_SOURCE_SYSTIMER_TARGET0, TICK_CPU_INT, 1, tick_handler);
}

uint32_t tick_count(void)
{
    return ticks;
}
