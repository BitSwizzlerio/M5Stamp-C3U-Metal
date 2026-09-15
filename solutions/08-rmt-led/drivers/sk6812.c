/*
 * sk6812.c - the SK6812 RGB LED, driven by the RMT peripheral.
 * Solution to exercise 8: this replaces the bit-banging in sk6812.S.
 *
 * RMT ("remote control") plays a list of symbols on a pin. Each symbol is two
 * (level, duration) pairs, so one symbol is exactly one SK6812 bit: high for
 * a while, then low. The hardware does the timing, so it no longer matters
 * how fast the CPU runs, where the code lives, or whether interrupts happen.
 *
 * Read first: drivers/sk6812.S in the project, which this replaces.
 */
#include <stdint.h>
#include "board.h"
#include "esp32c3-regs.h"
#include "gpio.h"
#include "sk6812.h"
#include "uptime.h"

/* One tick: the 40 MHz crystal divided by 2 is 20 MHz, so a tick is 50 ns. */
#define DIVIDER         2
#define TICK_NS         50

/* A symbol: bits 14:0 duration and bit 15 level of the first half; bits 30:16 and bit 31 of the second half. */
#define SYMBOL(high_ns, low_ns)     ((1u << 15) | ((high_ns) / TICK_NS) | (((low_ns) / TICK_NS) << 16))
#define BIT_0           SYMBOL(400, 850)        /* 0x00118008 */
#define BIT_1           SYMBOL(800, 450)        /* 0x00098010 */
#define END_MARKER      0                       /* a duration of 0 ends the list */

void sk6812_init(void)
{
    /* Give the RMT block its clock, and reset it. */
    REG(SYSTEM_PERIP_CLK_EN0) |= SYSTEM_RMT_CLK_EN;
    REG(SYSTEM_PERIP_RST_EN0) |= SYSTEM_RMT_RST;
    REG(SYSTEM_PERIP_RST_EN0) &= ~SYSTEM_RMT_RST;

    /* The whole block: clock from the crystal, divided by 1 + 0 + 0/1, and direct access to the symbol memory. */
    REG(RMT_SYS_CONF) = RMT_CLK_EN | RMT_SCLK_ACTIVE | RMT_SCLK_SEL_XTAL
                      | (1u << RMT_SCLK_DIV_B_S) | RMT_APB_FIFO_MASK;

    /* Channel 0: 50 ns ticks, one memory block, low while idle, no carrier. */
    REG(RMT_CH0CONF0) = (DIVIDER << RMT_DIV_CNT_CH0_S) | (1u << RMT_MEM_SIZE_CH0_S)
                      | RMT_IDLE_OUT_EN_CH0 | RMT_CONF_UPDATE_CH0;

    /* The LED pin: an output, driven by RMT channel 0 instead of the GPIO_OUT register. */
    gpio_output(LED_PIN);
    REG(GPIO_FUNC_OUT_SEL_CFG(LED_PIN)) = RMT_SIG_OUT0_IDX | GPIO_FUNC_OEN_SEL;

    /* Low for a while, so the LED expects a new colour; then off. */
    uint64_t end = uptime_cycles() + CPU_CYCLES_PER_MS;
    while (uptime_cycles() < end) {
    }
    sk6812_send_grb(0);
}

void sk6812_send_grb(uint32_t grb)
{
    volatile uint32_t *symbols = (volatile uint32_t *)RMTMEM_CH0;

    REG(RMT_INT_CLR) = RMT_CH0_TX_END_INT | RMT_CH0_ERR_INT;
    REG(RMT_CH0CONF0) |= RMT_MEM_RD_RST_CH0 | RMT_APB_MEM_RST_CH0;      /* start from the first symbol */
    REG(RMT_CH0CONF0) &= ~(RMT_MEM_RD_RST_CH0 | RMT_APB_MEM_RST_CH0);

    for (int i = 0; i < 24; i++)                                        /* most significant bit first */
        symbols[i] = (grb & (1u << (23 - i))) ? BIT_1 : BIT_0;
    symbols[24] = END_MARKER;

    REG(RMT_CH0CONF0) |= RMT_CONF_UPDATE_CH0 | RMT_TX_START_CH0;

    /* Sending takes 30 microseconds. Give up after 50 ms rather than hang if something is wrong. */
    uint64_t give_up = uptime_cycles() + 50 * (uint64_t)CPU_CYCLES_PER_MS;
    while (!(REG(RMT_INT_RAW) & (RMT_CH0_TX_END_INT | RMT_CH0_ERR_INT)) && uptime_cycles() < give_up) {
    }
}
