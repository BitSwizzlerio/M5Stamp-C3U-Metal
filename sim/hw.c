/*
 * hw.c - just enough of an ESP32-C3 for the ladder engine to run on a PC or
 * in a browser: the GPIO registers, the IO_MUX, and a clock.
 *
 * The engine (app/plc.c) talks to the pins through REG(), which sim/host/
 * esp32c3-regs.h points at sim_reg() below. Writes to GPIO_OUT_W1TS and
 * GPIO_OUT_W1TC, and to GPIO_ENABLE_W1TS and _W1TC, are carried out by
 * sim_settle(), which also works out GPIO_IN: an output pin reads back what it
 * drives, and any other pin reads what the simulated world puts on it, its
 * pull resistor's level unless something has been "wired" to it with
 * sim_set_input(). The button, G9, has a pull-up, so it reads 1 until pressed.
 *
 * Time only moves when sim_advance_us() moves it, so a test or a browser
 * decides how fast the simulated board runs.
 */
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"
#include "sim.h"
#include "uptime.h"

#define GPIO_BASE       0x60004000u
#define IO_MUX_BASE     0x60009000u
#define SLOT(addr)      (((addr) - GPIO_BASE) / 4u)

static uint32_t gpio[0x1000 / 4];               /* 0x60004000 to 0x60004FFF */
static uint32_t io_mux[0x100 / 4];              /* 0x60009000 to 0x600090FF */
static uint32_t other;                          /* anything else reads 0 and ignores writes */
static uint32_t driven_mask, driven_levels;     /* inputs "wired" by sim_set_input() */
static uint64_t now_cycles;

volatile uint32_t *sim_reg(uint32_t address)
{
    /* The chip carries out a write to a W1TS or W1TC register at once. REG() can't
       see the write itself, only the access, so every access first carries out
       the one before: by then it has landed. */
    sim_settle();
    if (address >= GPIO_BASE && address < GPIO_BASE + sizeof gpio)
        return &gpio[SLOT(address)];
    if (address >= IO_MUX_BASE && address < IO_MUX_BASE + sizeof io_mux)
        return &io_mux[(address - IO_MUX_BASE) / 4u];
    other = 0;
    return &other;
}

/* The level a pin's pull resistor gives it: IO_MUX bit 8 is the pull-up, 7 the pull-down. */
static uint32_t pulled_levels(void)
{
    uint32_t levels = 0;

    for (int pin = 0; pin < 22; pin++)
        if (io_mux[(4u + 4u * (unsigned)pin) / 4u] & (1u << 8))
            levels |= 1u << pin;
    return levels | (1u << BTN_PIN);            /* the board's own pull-up on the button */
}

void sim_settle(void)
{
    uint32_t *out = &gpio[SLOT(GPIO_OUT_W1TS) - 1];         /* GPIO_OUT is the word before W1TS */
    uint32_t *enable = &gpio[SLOT(GPIO_ENABLE_W1TS) - 1];   /* likewise GPIO_ENABLE */

    *out = (*out | gpio[SLOT(GPIO_OUT_W1TS)]) & ~gpio[SLOT(GPIO_OUT_W1TC)];
    gpio[SLOT(GPIO_OUT_W1TS)] = gpio[SLOT(GPIO_OUT_W1TC)] = 0;
    *enable = (*enable | gpio[SLOT(GPIO_ENABLE_W1TS)]) & ~gpio[SLOT(GPIO_ENABLE_W1TC)];
    gpio[SLOT(GPIO_ENABLE_W1TS)] = gpio[SLOT(GPIO_ENABLE_W1TC)] = 0;

    uint32_t outside = (pulled_levels() & ~driven_mask) | (driven_levels & driven_mask);
    gpio[SLOT(GPIO_IN)] = (*out & *enable) | (outside & ~*enable);
}

void sim_set_input(int pin, int level)
{
    if (level < 0) {                            /* -1: nothing wired, the pull resistor decides */
        driven_mask &= ~(1u << pin);
    } else {
        driven_mask |= 1u << pin;
        driven_levels = level ? (driven_levels | 1u << pin) : (driven_levels & ~(1u << pin));
    }
    sim_settle();
}

uint32_t sim_pins(void)    { sim_settle(); return gpio[SLOT(GPIO_IN)]; }
uint32_t sim_outputs(void) { return gpio[SLOT(GPIO_ENABLE_W1TS) - 1]; }   /* GPIO_ENABLE: which pins drive */

void sim_advance_us(uint64_t us) { now_cycles += us * CPU_CYCLES_PER_US; }
uint64_t sim_now_us(void)        { return now_cycles / CPU_CYCLES_PER_US; }

void sim_reset(void)
{
    memset(gpio, 0, sizeof gpio);
    memset(io_mux, 0, sizeof io_mux);
    driven_mask = driven_levels = 0;
    sim_settle();
}

/* What the engine calls instead of the real clock (drivers/uptime.c, chip/cpu.S). */
uint64_t uptime_cycles(void) { return now_cycles; }
uint64_t uptime_us(void)     { return now_cycles / CPU_CYCLES_PER_US; }
uint32_t cycle_count(void)   { return (uint32_t)now_cycles; }
void cycle_counter_start(void) {}
