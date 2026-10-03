/*
 * i2c.c - an I2C bus made by hand on two ordinary pins ("bit-banged").
 * Solution to exercise 10.
 *
 * I2C needs two wires, SDA (data) and SCL (clock), shared by every device on
 * the bus. We make the clock; each bit of data is set while SCL is low and read
 * while it is high. A transfer begins with a START (SDA falling while SCL is
 * high), then the device's 7-bit address and a read/write bit, then bytes, each
 * answered by whoever receives it, and ends with a STOP (SDA rising while SCL is
 * high).
 *
 * The chip has an I2C peripheral that does all this in hardware, but doing it by
 * hand shows every step. Nothing here needs exact timing: the devices follow our
 * clock, however unevenly it ticks, so this is ordinary code in flash rather than
 * the cycle-counted RAM code that sk6812.S has to be.
 *
 * Read first: drivers/gpio.c, whose functions are all this uses.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "cpu.h"
#include "gpio.h"
#include "i2c.h"

#define STRETCH_CYCLES  CPU_CYCLES_PER_MS       /* how long a device may hold SCL low to make us wait */

static int sda_pin, scl_pin;
static uint32_t half;                           /* half a clock period, in CPU cycles */

static void wait(uint32_t cycles)
{
    uint32_t start = cycle_count();

    while (cycle_count() - start < cycles) {
    }
}

/*
 * The lines are never driven high. Each has a pull-up resistor, and any device
 * may pull a line low, so "high" means letting go and letting the resistor do it.
 * Two devices can then never fight, one driving high and the other low. This is
 * called open drain.
 */
static void let_go(int pin)
{
    gpio_input(pin, GPIO_PULL_UP);
}

static void pull_low(int pin)
{
    gpio_output(pin);                           /* an output starts low */
}

/* Let go of SCL and wait until it really is high: a slow device may hold it low ("clock stretching"). */
static bool scl_up(void)
{
    uint32_t start = cycle_count();

    let_go(scl_pin);
    while (!gpio_read(scl_pin))
        if (cycle_count() - start > STRETCH_CYCLES)
            return false;
    return true;
}

/* START: SDA falls while SCL is high. Called with SCL low, it is a "repeated start". */
static enum i2c_result start(void)
{
    let_go(sda_pin);
    wait(half);
    if (!scl_up())
        return I2C_STUCK;
    wait(half);
    if (!gpio_read(sda_pin))                    /* something is holding SDA low */
        return I2C_STUCK;
    pull_low(sda_pin);
    wait(half);
    pull_low(scl_pin);
    return I2C_OK;
}

/* STOP: SDA rises while SCL is high. Afterwards both lines are high and the bus is free. */
static enum i2c_result stop(void)
{
    pull_low(sda_pin);
    wait(half);
    if (!scl_up())
        return I2C_STUCK;
    wait(half);
    let_go(sda_pin);
    wait(half);
    return gpio_read(sda_pin) ? I2C_OK : I2C_STUCK;
}

/* One bit out: set SDA while SCL is low, then one clock pulse. */
static bool bit_out(bool bit)
{
    if (bit)
        let_go(sda_pin);
    else
        pull_low(sda_pin);
    wait(half);
    if (!scl_up())
        return false;
    wait(half);
    pull_low(scl_pin);
    return true;
}

/* One bit in: let go of SDA so the device can set it, and read it while SCL is high. -1 if stuck. */
static int bit_in(void)
{
    int bit;

    let_go(sda_pin);
    wait(half);
    if (!scl_up())
        return -1;
    wait(half);
    bit = gpio_read(sda_pin);
    pull_low(scl_pin);
    return bit;
}

/* A byte out, most significant bit first. On the ninth clock the device pulls SDA low to say it got it (ACK). */
static enum i2c_result byte_out(uint8_t byte)
{
    int ack;

    for (int i = 7; i >= 0; i--)
        if (!bit_out((byte >> i) & 1))
            return I2C_STUCK;
    ack = bit_in();
    if (ack < 0)
        return I2C_STUCK;
    return ack == 0 ? I2C_OK : I2C_NACK;
}

/* A byte in. Then we answer: ACK for "more, please", or leave SDA high (NACK) after the last one. -1 if stuck. */
static int byte_in(bool more)
{
    int byte = 0;

    for (int i = 0; i < 8; i++) {
        int bit = bit_in();
        if (bit < 0)
            return -1;
        byte = (byte << 1) | bit;
    }
    return bit_out(!more) ? byte : -1;
}

/* End a transfer: a STOP if the bus still works, and both lines let go whatever happened. */
static enum i2c_result finish(enum i2c_result result)
{
    if (result != I2C_STUCK && stop() == I2C_STUCK)
        result = I2C_STUCK;
    let_go(sda_pin);
    let_go(scl_pin);
    return result;
}

void i2c_setup(int sda, int scl, uint32_t khz)
{
    sda_pin = sda;
    scl_pin = scl;
    /*
     * A period is 1000 / khz microseconds. Each wait is at least half of that, and
     * changing a pin takes time on top, so the clock runs slower than khz: about half
     * as fast at 100 kHz. Slower is always allowed in I2C; too short a half-period
     * is not, which is why the waits don't try to make up for the extra time.
     */
    half = CPU_CYCLES_PER_US * 500u / khz;
    let_go(sda);
    let_go(scl);
}

enum i2c_result i2c_write(int addr, const uint8_t *data, size_t len, bool stop_after)
{
    enum i2c_result result = start();

    if (result == I2C_OK)
        result = byte_out((uint8_t)(addr << 1));            /* bit 0 is 0: we will write */
    for (size_t i = 0; result == I2C_OK && i < len; i++)
        result = byte_out(data[i]);
    if (result == I2C_OK && !stop_after)
        return I2C_OK;                                      /* keep the bus for a read */
    return finish(result);
}

enum i2c_result i2c_read(int addr, uint8_t *data, size_t len)
{
    enum i2c_result result = start();

    if (result == I2C_OK)
        result = byte_out((uint8_t)((addr << 1) | 1));      /* bit 0 is 1: we will read */
    for (size_t i = 0; result == I2C_OK && i < len; i++) {
        int byte = byte_in(i + 1 < len);
        if (byte < 0)
            result = I2C_STUCK;
        else
            data[i] = (uint8_t)byte;
    }
    return finish(result);
}
