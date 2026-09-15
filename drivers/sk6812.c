/*
 * sk6812.c - set up the SK6812 RGB LED. The timed code that sends a colour
 * is in sk6812.S.
 */
#include "board.h"
#include "gpio.h"
#include "sk6812.h"
#include "uptime.h"

void sk6812_init(void)
{
    gpio_output(LED_PIN);                       /* starts low */

    /* Holding the data line low for a while tells the LED that the next bits
       start a new colour. 1 ms is comfortably long enough. */
    uint64_t end = uptime_cycles() + CPU_CYCLES_PER_MS;
    while (uptime_cycles() < end) {
    }
    sk6812_send_grb(0);                         /* off */
}
