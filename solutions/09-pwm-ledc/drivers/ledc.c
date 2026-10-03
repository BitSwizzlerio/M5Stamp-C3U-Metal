/*
 * ledc.c - steady pulses (PWM) from the LEDC peripheral.
 * Solution to exercise 9.
 *
 * LEDC ("LED control") was made for dimming LEDs, but its output is plain pulse
 * width modulation, which also sets a motor's speed, a servo's angle or a buzzer's
 * note. Each channel compares a timer's count with two numbers: the output goes
 * high when the count reaches HPOINT and low DUTY counts later. The timer counts
 * 2^bits steps and starts again, and that is one period.
 *
 * Unlike sk6812.S, which times each pulse by counting CPU cycles, nothing here is
 * timing-critical for the CPU: once a channel is set up, the hardware keeps the
 * pulses going by itself while the program does something else.
 *
 * The timers count the 40 MHz crystal, not the CPU's clock, so the frequencies
 * stay right whatever speed the CPU runs at (exercise 2).
 *
 * Read first: drivers/sk6812.S in the project, which makes its pulses by hand.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp32c3-regs.h"
#include "gpio.h"
#include "ledc.h"

#define XTAL_HZ         40000000u       /* the crystal, whatever the CPU runs at */
/*
 * The C3's timers can count 14 bits, but a hardware bug means a duty of exactly 2^14
 * (always high) comes out as always low. ESP-IDF's ledc.c says so too. With 13 bits,
 * 100% works.
 */
#define MAX_BITS        13
#define DIVIDER_ONE     256u            /* the divider has 8 fraction bits: 256 means 1.0 */
#define DIVIDER_MAX     0x3FFFFu        /* 18 bits: just under 1024 */

static uint32_t bits[LEDC_CHANNELS];    /* each channel's timer counts 2^bits steps per period */

void ledc_init(void)
{
    /* Give the LEDC block its clock, and reset it, which also stops anything left running. */
    REG(SYSTEM_PERIP_CLK_EN0) |= SYSTEM_LEDC_CLK_EN;
    REG(SYSTEM_PERIP_RST_EN0) |= SYSTEM_LEDC_RST;
    REG(SYSTEM_PERIP_RST_EN0) &= ~SYSTEM_LEDC_RST;

    REG(LEDC_CONF) = LEDC_CLK_EN | LEDC_CLK_SEL_XTAL;   /* every timer counts the crystal */
}

void ledc_start(int ch, int pin, uint32_t hz, uint32_t duty)
{
    /*
     * The timer. hz = XTAL_HZ / (divider * 2^bits), and the divider must be at least 1.
     * More bits means finer steps of duty, so start with the most and give up bits
     * until the divider is big enough. At 40 kHz that leaves 9 bits; at 4.8 kHz and
     * below, all 13.
     */
    uint32_t n = MAX_BITS, divider;
    for (;;) {
        divider = (uint32_t)(((uint64_t)XTAL_HZ * DIVIDER_ONE) / ((uint64_t)hz << n));
        if (divider >= DIVIDER_ONE || n == 1)
            break;
        n--;
    }
    if (divider > DIVIDER_MAX)
        divider = DIVIDER_MAX;
    bits[ch] = n;

    REG(LEDC_TIMER_CONF(ch)) = (n << LEDC_DUTY_RES_S) | (divider << LEDC_CLK_DIV_S) | LEDC_TIMER_RST;
    REG(LEDC_TIMER_CONF(ch)) &= ~LEDC_TIMER_RST;        /* count from zero ... */
    REG(LEDC_TIMER_CONF(ch)) |= LEDC_TIMER_PARA_UP;     /* ... with the new settings */

    /* The channel, using the timer with its own number. */
    ledc_set_duty(ch, duty);

    /* The pin: an output, driven by this channel instead of by the GPIO_OUT register. */
    gpio_output(pin);
    REG(GPIO_FUNC_OUT_SEL_CFG(pin)) = (LEDC_SIG_OUT0_IDX + ch) | GPIO_FUNC_OEN_SEL;
}

void ledc_set_duty(int ch, uint32_t duty)
{
    uint32_t counts = (uint32_t)(((uint64_t)duty << bits[ch]) / LEDC_FULL);

    REG(LEDC_CH_HPOINT(ch)) = 0;                        /* high from the start of each period */
    REG(LEDC_CH_DUTY(ch)) = counts << 4;                /* the register has 4 fraction bits */
    /* No fading: one step of no change. DUTY_START makes the new duty take effect. */
    REG(LEDC_CH_CONF1(ch)) = LEDC_DUTY_START | LEDC_DUTY_INC
                           | (1u << LEDC_DUTY_NUM_S) | (1u << LEDC_DUTY_CYCLE_S);
    REG(LEDC_CH_CONF0(ch)) = ((uint32_t)ch << LEDC_TIMER_SEL_S) | LEDC_SIG_OUT_EN | LEDC_CH_PARA_UP;
}

void ledc_stop(int ch, int pin)
{
    REG(LEDC_CH_CONF0(ch)) = ((uint32_t)ch << LEDC_TIMER_SEL_S) | LEDC_CH_PARA_UP;  /* output off: idle, low */
    REG(GPIO_FUNC_OUT_SEL_CFG(pin)) = SIG_GPIO_OUT_IDX;                            /* GPIO_OUT drives the pin again */
    gpio_write(pin, false);
}
