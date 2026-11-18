/*
 * status.c - keep board_status up to date for the C3U Viewer. See status.h.
 *
 * Everything here is something the program could already tell you at the
 * console: mem() prints the heap and the stack, millis() the uptime. The only
 * new thing is where the numbers are kept: at a fixed address, so that a
 * program on the PC can read them over JTAG without stopping the CPU.
 *
 * Read first: app/status.h, then boot/stack_check.c (how the stack's
 * high-water mark is found).
 * Try this:   peek(0x3FCBDF1C) is heap_in_use. Make a big table, t = {} for
 *             i = 1, 5000 do t[i] = i end, and read it again.
 */
#include <malloc.h>
#include <string.h>
#include "board.h"
#include "esp32c3-regs.h"
#include "linker_symbols.h"
#include "stack_check.h"
#include "status.h"
#include "syscalls.h"
#include "uptime.h"

#define UPDATE_EVERY_MS     250

/* At STATUS_ADDRESS, set in boot/c3u-metal.ld, where the viewer finds it. */
struct board_status board_status __attribute__((section(".board_status")));

static void start(void)
{
    struct board_status *s = &board_status;

    memset(s, 0, sizeof *s);            /* its section isn't cleared at start-up like .bss */
    s->magic = STATUS_MAGIC;
    s->layout = STATUS_LAYOUT;
    s->cpu_mhz = CPU_MHZ;
    s->reset_reason = REG(RTC_CNTL_RESET_STATE) & RTC_CNTL_RESET_CAUSE;
    s->heap_size = (uint32_t)(_heap_end - _heap_start);
    s->stack_size = (uint32_t)stack_size();
    strncpy(s->built, __DATE__ " " __TIME__, sizeof s->built - 1);
}

static void update(uint64_t now)
{
    struct board_status *s = &board_status;
    struct mallinfo info = mallinfo();  /* newlib's malloc statistics, as mem() uses */

    s->uptime_ms = (uint32_t)(now / CPU_CYCLES_PER_MS);
    s->heap_in_use = (uint32_t)info.uordblks;
    s->heap_claimed = (uint32_t)((char *)_sbrk(0) - _heap_start);
    s->stack_max_used = (uint32_t)stack_max_used();
    s->stack_overflowed = stack_overflowed();
    s->updates++;                       /* last: the viewer sees a new number once everything is in */
}

void status_poll(void)
{
    static bool started;
    static uint64_t next_update;
    uint64_t now = uptime_cycles();

    if (!started) {
        start();
        started = true;
    }
    if (now < next_update)
        return;
    next_update = now + (uint64_t)UPDATE_EVERY_MS * CPU_CYCLES_PER_MS;
    update(now);
}
