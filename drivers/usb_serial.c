/*
 * usb_serial.c - a console over the ESP32-C3's built-in USB Serial/JTAG port.
 *
 * Polling only, no interrupts. The hardware has a 64-byte send buffer: bytes
 * written to EP1 wait there until WR_DONE is written, or until the buffer is
 * full, and then go to the PC as one USB packet. Received bytes are read one at
 * a time from the same EP1 register.
 */
#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"
#include "usb_serial.h"

#define PACKET_SIZE         64
#define TX_TIMEOUT_CYCLES   (CPU_CYCLES_PER_MS * 50)    /* 50 ms */

static unsigned pending;            /* bytes in the send buffer since the last flush */
static bool     last_packet_full;   /* the hardware just sent a full 64-byte packet by itself */
static bool     nobody_listening;   /* the last wait timed out */

/*
 * Wait until the send buffer has room. If no terminal is reading, it never
 * will, so give up after TX_TIMEOUT_CYCLES and then stop waiting altogether
 * (dropping output) until room appears again. Otherwise every character would
 * cost 50 ms whenever the terminal is closed.
 */
static bool wait_for_room(void)
{
    if (REG(USB_SERIAL_EP1_CONF) & USB_SERIAL_TX_FREE) {
        nobody_listening = false;
        return true;
    }
    if (nobody_listening)
        return false;

    uint32_t start = cycle_count();
    while (!(REG(USB_SERIAL_EP1_CONF) & USB_SERIAL_TX_FREE)) {
        if (cycle_count() - start >= TX_TIMEOUT_CYCLES) {
            nobody_listening = true;
            return false;
        }
    }
    return true;
}

void usb_serial_putc(char c)
{
    if (!wait_for_room())
        return;
    REG(USB_SERIAL_EP1) = (uint8_t)c;
    pending++;
    last_packet_full = false;
    if (pending == PACKET_SIZE) {       /* a full buffer is sent automatically */
        pending = 0;
        last_packet_full = true;
    }
}

void usb_serial_flush(void)
{
    if (pending > 0) {
        REG(USB_SERIAL_EP1_CONF) = USB_SERIAL_WR_DONE;
        pending = 0;
    } else if (last_packet_full) {
        /*
         * A packet of exactly 64 bytes tells the PC "more is coming", so the
         * terminal waits. An empty packet (WR_DONE with nothing buffered)
         * ends the transfer and makes it show the data.
         */
        if (wait_for_room())
            REG(USB_SERIAL_EP1_CONF) = USB_SERIAL_WR_DONE;
        last_packet_full = false;
    }
}

void usb_serial_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n')
            usb_serial_putc('\r');      /* terminals expect CR LF */
        usb_serial_putc(buf[i]);
    }
    usb_serial_flush();
}

int usb_serial_getc(void)
{
    if (REG(USB_SERIAL_EP1_CONF) & USB_SERIAL_RX_AVAIL)
        return REG(USB_SERIAL_EP1) & 0xFF;
    return -1;
}
