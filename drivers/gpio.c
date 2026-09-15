/*
 * gpio.c - the ESP32-C3's pins as plain digital inputs and outputs.
 *
 * Two blocks of registers are involved:
 *   IO_MUX  one register per pin decides what its pad is connected to (plain
 *           GPIO, or a peripheral such as SPI) and switches the pad's input
 *           buffer and pull resistors on or off.
 *   GPIO    registers with one bit per pin set the output level, turn the
 *           output driver on or off, and read the level. For outputs, a
 *           per-pin FUNCn_OUT_SEL_CFG register picks which signal drives the
 *           pin; SIG_GPIO_OUT_IDX means "the GPIO_OUT register".
 */
#include <stddef.h>
#include <stdint.h>
#include "esp32c3-regs.h"
#include "gpio.h"

#define GPIO_COUNT  22                          /* GPIO0 to GPIO21 */

const char *gpio_check_pin(int pin)
{
    if (pin < 0 || pin >= GPIO_COUNT)
        return "the ESP32-C3 has GPIO0 to GPIO21";
    if (pin == 11)
        return "GPIO11 powers the flash chip (VDD_SPI)";
    if (pin >= 12 && pin <= 17)
        return "GPIO12 to GPIO17 are connected to the flash chip";
    if (pin == 18 || pin == 19)
        return "GPIO18 and GPIO19 are the USB port (the console)";
    return NULL;
}

/* Connect the pad to GPIO and set its input-enable and pull-resistor bits. */
static void pad_config(int pin, uint32_t bits)
{
    uint32_t pad = IO_MUX_GPIO(pin);

    REG(pad) = (REG(pad) & ~IO_MUX_CLEAR_MASK) | IO_MUX_MCU_SEL_GPIO | bits;
}

void gpio_output(int pin)
{
    pad_config(pin, IO_MUX_FUN_IE);             /* input stays on, so gpio_read() sees what we drive */
    REG(GPIO_FUNC_OUT_SEL_CFG(pin)) = SIG_GPIO_OUT_IDX;
    REG(GPIO_OUT_W1TC) = 1u << pin;             /* low first ...                */
    REG(GPIO_ENABLE_W1TS) = 1u << pin;          /* ... then turn the driver on  */
}

void gpio_input(int pin, enum gpio_pull pull)
{
    uint32_t bits = IO_MUX_FUN_IE;

    if (pull == GPIO_PULL_UP)
        bits |= IO_MUX_FUN_PU;
    else if (pull == GPIO_PULL_DOWN)
        bits |= IO_MUX_FUN_PD;
    REG(GPIO_ENABLE_W1TC) = 1u << pin;          /* output driver off */
    pad_config(pin, bits);
}

void gpio_write(int pin, bool high)
{
    /* W1TS/W1TC: "write 1 to set" and "write 1 to clear" change only the pins
       whose bits are 1, so no read-modify-write is needed. */
    if (high)
        REG(GPIO_OUT_W1TS) = 1u << pin;
    else
        REG(GPIO_OUT_W1TC) = 1u << pin;
}

bool gpio_read(int pin)
{
    return (REG(GPIO_IN) >> pin) & 1u;
}
