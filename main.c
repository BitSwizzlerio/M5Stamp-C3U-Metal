/*
 * main.c - C3U-Metal, Phase 1 test: newlib and the USB console.
 *
 * Open a terminal on the board's COM port. Typed characters are echoed back;
 * pressing Enter prints a line using printf (integer, decimal and malloc).
 * The LED still lights while the button is held.
 *
 * Before main() runs, crt0.S (the C runtime) has set up sp and gp, called
 * SystemInit() (system_esp32c3.c: watchdogs off, CPU at 40 MHz, cycle counter
 * on), copied RAM code and .data, cleared .bss and run newlib's start-up functions.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp32c3-regs.h"
#include "usb_serial.h"

#define LED_PIN         2
#define BTN_PIN         9
#define POLL_MS         20
#define CYCLES_PER_MS   40000u              /* CPU at 40 MHz */

/* Written in assembly */
void send_grb(uint32_t grb);                /* led.S */
uint32_t cycle_count(void);                 /* cpu.S */

uint32_t led_color = 0x002000;              /* 0x00GGRRBB: dim red */
uint32_t loop_count;

static void led_pin_init(void)
{
    REG(IO_MUX_GPIO2) = (REG(IO_MUX_GPIO2) & ~IO_MUX_CLEAR_MASK) | IO_MUX_MCU_SEL_GPIO;
    REG(GPIO_FUNC2_OUT_SEL_CFG) = SIG_GPIO_OUT_IDX;     /* GPIO2 follows the GPIO_OUT register */
    REG(GPIO_OUT_W1TC) = 1u << LED_PIN;                  /* output low first ...        */
    REG(GPIO_ENABLE_W1TS) = 1u << LED_PIN;               /* ... then turn the driver on */
}

static void button_pin_init(void)
{
    REG(IO_MUX_GPIO9) = (REG(IO_MUX_GPIO9) & ~IO_MUX_CLEAR_MASK)
                      | IO_MUX_MCU_SEL_GPIO | IO_MUX_FUN_IE | IO_MUX_FUN_PU;
    REG(GPIO_ENABLE_W1TC) = 1u << BTN_PIN;               /* input only */
}

static bool button_pressed(void)
{
    return (REG(GPIO_IN) & (1u << BTN_PIN)) == 0;        /* pulled up, so 0 means pressed */
}

int main(void)
{
    led_pin_init();
    button_pin_init();

    printf("\nC3U-Metal: newlib + USB console. Type something, then press Enter.\n");

    uint32_t last_poll = cycle_count();
    for (;;) {
        int c = usb_serial_getc();

        if (c == '\r' || c == '\n') {
            char *p = malloc(1000);
            printf("\nhello %d %g, malloc(1000) = %p, loops %lu\n",
                   42, 3.5, (void *)p, (unsigned long)loop_count);
            free(p);
        } else if (c >= 0) {
            putchar(c);                     /* echo */
            fflush(stdout);
        }

        if (cycle_count() - last_poll >= POLL_MS * CYCLES_PER_MS) {
            last_poll = cycle_count();
            send_grb(button_pressed() ? led_color : 0);
            loop_count++;
        }
    }
}
