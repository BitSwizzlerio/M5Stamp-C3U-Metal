/*
 * main.c - C3U-Metal: the RGB LED lights while the button is held.
 *
 * Before main() runs, crt0.S (the C runtime) has set up sp and gp, called
 * SystemInit() (system_esp32c3.c: watchdogs off, CPU at 40 MHz, cycle counter
 * on), copied .data and cleared .bss. There is no C library: hardware is
 * reached through REG().
 *
 *   Button : GPIO9, reads 0 while pressed
 *   LED    : GPIO2, one SK6812 addressable LED
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp32c3-regs.h"

#define LED_PIN         2
#define BTN_PIN         9
#define POLL_MS         20
#define CYCLES_PER_MS   40000u              /* CPU at 40 MHz */

/* Written in assembly */
void send_grb(uint32_t grb);                /* led.S */
uint32_t cycle_count(void);                 /* cpu.S */

/*
 * One variable of each kind, so the runtime can be checked on the board:
 * the LED only shows led_color if .data was copied, loop_count only counts up from
 * 0 if .bss was cleared, and build_name is only readable if .rodata is mapped.
 */
uint32_t led_color = 0x002000;              /* .data  (0x00GGRRBB: dim red) */
uint32_t loop_count;                        /* .bss */
__attribute__((used))
static const char build_name[] = "C3U-Metal C runtime";   /* .rodata */

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

static void delay_ms(uint32_t ms)
{
    uint32_t start = cycle_count();

    while (cycle_count() - start < ms * CYCLES_PER_MS) {
        /* busy-wait: nothing else to do */
    }
}

int main(void)
{
    led_pin_init();
    button_pin_init();

    for (;;) {
        send_grb(button_pressed() ? led_color : 0);
        delay_ms(POLL_MS);
        loop_count++;
    }
}
