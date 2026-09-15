/*
 * interrupts.h - connect a peripheral's interrupt to a C function.
 * Part of the solution to exercise 4.
 *
 * The path of an interrupt on the ESP32-C3:
 *   1. a peripheral (such as the SYSTIMER) raises its interrupt flag;
 *   2. the interrupt matrix sends that source to one of the CPU's 31 interrupts;
 *   3. if that CPU interrupt is enabled, its priority is at least the threshold,
 *      and mstatus.MIE is set, the CPU jumps to that entry of trap_vector_table;
 *   4. interrupt_entry (chip/trap.S) saves registers and calls interrupt_dispatch(),
 *      which calls the handler attached here.
 */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* Interrupt source numbers, from ESP-IDF's components/soc/esp32c3/include/soc/interrupts.h */
#define INT_SOURCE_USB_SERIAL_JTAG      26
#define INT_SOURCE_SYSTIMER_TARGET0     37

/*
 * Send interrupt 'source' to CPU interrupt 'cpu_int' (2 to 31; avoid 6) with
 * 'priority' 1 to 15, and call 'handler' whenever it happens. The handler must
 * clear the peripheral's interrupt flag. Nothing happens until
 * cpu_interrupts_on() (cpu.h) lets interrupts through.
 */
void interrupt_attach(int source, int cpu_int, int priority, void (*handler)(void));

#endif
