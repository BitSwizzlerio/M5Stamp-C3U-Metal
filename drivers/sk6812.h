/*
 * sk6812.h - the SK6812 addressable RGB LED (the code is in sk6812.S).
 */
#ifndef SK6812_H
#define SK6812_H

#include <stdint.h>

/*
 * Send one colour to the LED on LED_PIN (board.h), as 0x00GGRRBB: the LED
 * expects green, then red, then blue. The pin must already be a GPIO output.
 * Takes about 30 microseconds.
 */
void sk6812_send_grb(uint32_t grb);

#endif
