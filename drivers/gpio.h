/*
 * gpio.h - the ESP32-C3's pins as plain digital inputs and outputs.
 */
#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

enum gpio_pull {
    GPIO_PULL_NONE,                             /* the pin floats when nothing drives it */
    GPIO_PULL_UP,                               /* a weak resistor pulls it high (about 45 kOhm) */
    GPIO_PULL_DOWN,                             /* a weak resistor pulls it low */
};

/*
 * NULL if 'pin' can be used, otherwise a message saying why not. The other
 * functions don't check, so call this first with any pin number a user typed.
 */
const char *gpio_check_pin(int pin);

void gpio_output(int pin);                      /* make the pin an output, starting low */
void gpio_input(int pin, enum gpio_pull pull);  /* make the pin an input */
void gpio_write(int pin, bool high);            /* drive an output high or low */
bool gpio_read(int pin);                        /* the level on the pin; works for outputs too */

#endif
