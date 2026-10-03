/*
 * i2c.h - an I2C bus made by hand on two ordinary pins ("bit-banged").
 * Solution to exercise 10.
 */
#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum i2c_result {
    I2C_OK,                     /* the device answered every byte */
    I2C_NACK,                   /* nothing answered: no device at that address, or it said no */
    I2C_STUCK,                  /* a line stayed low when it should have gone high */
};

void i2c_setup(int sda, int scl, uint32_t khz);    /* khz is the fastest the clock will go */

/* Send 'len' bytes to the device at 7-bit address 'addr'. With stop_after false,
   the bus is kept, so that i2c_read() follows with a repeated start. len 0 just
   asks whether anything answers at that address. */
enum i2c_result i2c_write(int addr, const uint8_t *data, size_t len, bool stop_after);

/* Read 'len' bytes from the device at 'addr', then let go of the bus. */
enum i2c_result i2c_read(int addr, uint8_t *data, size_t len);

#endif
