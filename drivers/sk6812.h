/*
 * sk6812.h - the SK6812 addressable RGB LED on LED_PIN (board.h).
 */
#ifndef SK6812_H
#define SK6812_H

#include <stdint.h>

/* sk6812.c: make LED_PIN an output and switch the LED off. Call this first. */
void sk6812_init(void);

/*
 * sk6812.S: send one colour as 0x00GGRRBB. The LED expects green, then red,
 * then blue. Takes about 30 microseconds.
 */
void sk6812_send_grb(uint32_t grb);

#endif
