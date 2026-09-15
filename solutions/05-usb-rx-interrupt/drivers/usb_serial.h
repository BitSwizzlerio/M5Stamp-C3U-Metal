/*
 * usb_serial.h - a console over the ESP32-C3's built-in USB Serial/JTAG port
 * (the COM port the PC sees when the C3U is plugged in).
 * In this solution to exercise 5, received bytes arrive by interrupt.
 */
#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stdbool.h>
#include <stddef.h>

void usb_serial_putc(char c);                           /* add one byte to the send buffer          */
void usb_serial_write(const char *buf, size_t len);     /* send text, turning \n into \r\n, then flush */
void usb_serial_flush(void);                            /* send whatever is buffered to the PC now  */
int  usb_serial_getc(void);                             /* next byte received, or -1 if none waiting */

void usb_serial_start_rx_interrupt(void);               /* receive by interrupt; call before cpu_interrupts_on() */
bool usb_serial_take_ctrl_c(void);                      /* true if Ctrl-C arrived since the last call */
void usb_serial_discard_input(void);                    /* forget everything received but not read yet */

#endif
