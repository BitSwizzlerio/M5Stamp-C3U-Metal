/*
 * status.h - a few numbers about the board, kept where the C3U Viewer can read them.
 *
 * The viewer reads memory over JTAG while the program runs, but it has nothing
 * from the build, so it can't find Lua's heap or the stack's high-water mark by
 * name. Instead the firmware keeps the numbers in one struct, board_status, at a
 * fixed address (boot/c3u-metal.ld puts it there), and updates them a few times
 * a second. The same idea as plc_image (app/plc.h), for the board itself.
 *
 * Try this:   hex(peek(0x3FCBDF00)) is the magic number, 0x54415453: "STAT".
 *             peek(0x3FCBDF0C) is the time since start-up, in ms. Read it twice.
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

#define STATUS_MAGIC    0x54415453u     /* "STAT" in memory: the viewer looks for it */
#define STATUS_LAYOUT   1               /* changes whenever struct board_status changes */
#define STATUS_ADDRESS  0x3FCBDF00u     /* where board_status lives: boot/c3u-metal.ld puts it there */

/* The order of the fields is fixed by STATUS_LAYOUT: the viewer reads them by position. */
struct board_status {
    uint32_t magic;                     /* STATUS_MAGIC */
    uint32_t layout;                    /* STATUS_LAYOUT */
    uint32_t updates;                   /* counts updates, about four a second: it keeps changing while the program runs */
    uint32_t uptime_ms;                 /* since the CPU started */
    uint32_t cpu_mhz;
    uint32_t reset_reason;              /* why the CPU last started: RTC_CNTL_RESET_STATE bits 5-0 */
    uint32_t heap_size;                 /* bytes between the variables and board_status */
    uint32_t heap_in_use;               /* bytes malloc has handed out, Lua's included */
    uint32_t heap_claimed;              /* how far malloc has claimed the heap so far */
    uint32_t stack_size;
    uint32_t stack_max_used;            /* the deepest the stack has been */
    uint32_t stack_overflowed;          /* 1 if the stack has grown into the heap */
    char     built[24];                 /* when this firmware was built: __DATE__ " " __TIME__ */
};

extern struct board_status board_status;

/*
 * Update the numbers, at most four times a second. The places that already
 * poll call this, next to plc_poll(): the prompt's wait for a key, and the
 * Ctrl-C check while Lua runs. The first call also fills in what never changes.
 */
void status_poll(void);

#endif
