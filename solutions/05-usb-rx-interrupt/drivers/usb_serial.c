/*
 * usb_serial.c - a console over the ESP32-C3's built-in USB Serial/JTAG port.
 * Solution to exercise 5: received bytes arrive by interrupt.
 *
 * Sending is unchanged: polling, into the hardware's 64-byte send buffer.
 * Receiving is new. When a USB packet arrives from the PC, the "packet
 * received" interrupt runs rx_handler(), which moves every byte into
 * rx_buffer. usb_serial_getc() takes bytes from there. The handler also spots
 * Ctrl-C the moment it arrives, so a running Lua program can be stopped
 * without the REPL reading the keyboard itself.
 *
 * Read first: chip/interrupts.h (from exercise 4) and docs/usb-console.md.
 */
#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"
#include "interrupts.h"
#include "usb_serial.h"

#define PACKET_SIZE         64
#define TX_TIMEOUT_CYCLES   (CPU_CYCLES_PER_MS * 50)    /* 50 ms */
#define RX_BUFFER_SIZE      1024                        /* a power of two, so indices wrap with & */
#define USB_CPU_INT         4                           /* any free CPU interrupt; the tick uses 3 */
#define CTRL_C              0x03


/* ---- Sending (as before) ------------------------------------------------ */

static unsigned pending;            /* bytes in the send buffer since the last flush */
static bool     last_packet_full;   /* the hardware just sent a full 64-byte packet by itself */
static bool     nobody_listening;   /* the last wait timed out */

/*
 * Wait until the send buffer has room. If no terminal is reading, it never
 * will, so give up after TX_TIMEOUT_CYCLES and then stop waiting altogether
 * (dropping output) until room appears again.
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
        /* After exactly 64 bytes, an empty packet tells the PC the transfer is over. */
        if (wait_for_room())
            REG(USB_SERIAL_EP1_CONF) = USB_SERIAL_WR_DONE;
        last_packet_full = false;
    }
}

void usb_serial_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r'))
            usb_serial_putc('\r');      /* terminals expect CR LF; keep one that is already there */
        usb_serial_putc(buf[i]);
    }
    usb_serial_flush();
}


/* ---- Receiving, by interrupt --------------------------------------------- */

/*
 * A ring buffer: the handler adds bytes at rx_head and usb_serial_getc() takes
 * them from rx_tail. Each side only ever changes its own index, so the handler
 * can run at any moment without the two getting in each other's way.
 */
static char rx_buffer[RX_BUFFER_SIZE];
static volatile unsigned rx_head, rx_tail;
static volatile unsigned ctrl_c_count;      /* Ctrl-Cs received so far */
static unsigned ctrl_c_seen;                /* how many of them usb_serial_take_ctrl_c() has reported */

static void rx_handler(void)
{
    while (REG(USB_SERIAL_EP1_CONF) & USB_SERIAL_RX_AVAIL) {
        char c = (char)(REG(USB_SERIAL_EP1) & 0xFF);
        unsigned next = (rx_head + 1) & (RX_BUFFER_SIZE - 1);

        if (c == CTRL_C)
            ctrl_c_count++;
        if (next != rx_tail) {          /* if the buffer is full, the byte is lost */
            rx_buffer[rx_head] = c;
            rx_head = next;
        }
    }
    REG(USB_SERIAL_INT_CLR) = USB_SERIAL_OUT_RECV_PKT;  /* after emptying the hardware, clear the flag */
}

void usb_serial_start_rx_interrupt(void)
{
    REG(USB_SERIAL_INT_CLR) = USB_SERIAL_OUT_RECV_PKT;
    REG(USB_SERIAL_INT_ENA) |= USB_SERIAL_OUT_RECV_PKT;
    interrupt_attach(INT_SOURCE_USB_SERIAL_JTAG, USB_CPU_INT, 1, rx_handler);
}

int usb_serial_getc(void)
{
    if (rx_tail == rx_head)
        return -1;
    int c = (unsigned char)rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) & (RX_BUFFER_SIZE - 1);
    return c;
}

bool usb_serial_take_ctrl_c(void)
{
    /* A count, not a flag: a Ctrl-C that arrives after this read is reported next time, never lost. */
    unsigned count = ctrl_c_count;
    bool arrived = (count != ctrl_c_seen);

    ctrl_c_seen = count;
    return arrived;
}

void usb_serial_discard_input(void)
{
    rx_tail = rx_head;
}
