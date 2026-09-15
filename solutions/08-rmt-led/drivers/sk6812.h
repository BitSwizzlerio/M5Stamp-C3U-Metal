/*
 * sk6812.h - the SK6812 addressable RGB LED on LED_PIN (board.h).
 * In this solution to exercise 8, both functions are in sk6812.c and use the RMT peripheral.
 */
#ifndef SK6812_H
#define SK6812_H

#include <stdint.h>

/* Set up the RMT peripheral and LED_PIN, and switch the LED off. Call this first. */
void sk6812_init(void);

/* Send one colour as 0x00GGRRBB: the LED expects green, then red, then blue. Takes about 30 microseconds. */
void sk6812_send_grb(uint32_t grb);

#endif
