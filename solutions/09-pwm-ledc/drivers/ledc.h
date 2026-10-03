/*
 * ledc.h - steady pulses (PWM) from the LEDC peripheral.
 * Solution to exercise 9.
 */
#ifndef LEDC_H
#define LEDC_H

#include <stdint.h>

#define LEDC_CHANNELS   4               /* the chip has 6 channels but 4 timers; one timer each */
#define LEDC_MIN_HZ     5               /* slower needs a divider bigger than the timer allows */
#define LEDC_MAX_HZ     40000           /* faster leaves fewer than 9 bits for the duty */
#define LEDC_FULL       10000           /* duties are in hundredths of a percent: 10000 is always high */

void ledc_init(void);                   /* clock on, every channel stopped */

/* Start channel 'ch' (0-3) on 'pin' at 'hz', high for 'duty' / LEDC_FULL of each period. */
void ledc_start(int ch, int pin, uint32_t hz, uint32_t duty);
void ledc_set_duty(int ch, uint32_t duty);
void ledc_stop(int ch, int pin);        /* the pin goes back to being a plain output, low */

#endif
