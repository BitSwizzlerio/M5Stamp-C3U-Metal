/*
 * esp32c3-regs.h for the PC and WebAssembly builds (sim/): the chip's own
 * register names, but REG() points at the simulated chip in sim/hw.c instead
 * of at real hardware. The ladder engine (app/plc*.c) and the GPIO driver
 * (drivers/gpio.c) compile unchanged against it.
 */
#ifndef SIM_ESP32C3_REGS_H
#define SIM_ESP32C3_REGS_H

#include <stdint.h>
#include "../../chip/esp32c3-regs.h"

#undef REG
volatile uint32_t *sim_reg(uint32_t address);  /* the simulated register at that address */
#define REG(addr)   (*sim_reg((uint32_t)(addr)))

#endif
