/*
 * syscalls.c - the functions newlib calls to reach the "operating system".
 *
 * newlib does the real C library work (printf, malloc, strings, maths), but to
 * actually move bytes or get memory it calls these. On C3U-Metal the console
 * is the USB serial port, the heap is a region defined in c3u-metal.ld, and
 * there are no files, processes or real-time clock.
 */
#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include "usb_serial.h"

uint32_t cycle_count(void);                             /* cpu.S */

#define CYCLES_PER_US   40u                             /* CPU at 40 MHz */

extern char _heap_start[];                              /* c3u-metal.ld */
extern char _heap_end[];

/* newlib as built for ESP-IDF finds its per-task state through this. There is only one "task". */
struct _reent *__getreent(void)
{
    return _impure_ptr;
}

/* malloc() asks for more memory with this: move the end of the used heap by 'increment' bytes. */
void *_sbrk(ptrdiff_t increment)
{
    static char *brk = _heap_start;
    char *old = brk;

    if (increment > _heap_end - brk || increment < _heap_start - brk) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += increment;
    return old;
}

/* stdout (1) and stderr (2) go to the USB console. */
int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    usb_serial_write(buf, (size_t)len);
    return len;
}

/* stdin (0) comes from the USB console: wait for at least one byte. */
int _read(int fd, char *buf, int len)
{
    int c;

    if (fd != 0) {
        errno = EBADF;
        return -1;
    }
    if (len <= 0)
        return 0;
    while ((c = usb_serial_getc()) < 0) {
        /* nothing typed yet */
    }
    buf[0] = (char)c;
    return 1;
}

/* The three console streams are character devices, which makes newlib line-buffer stdout. */
int _fstat(int fd, struct stat *st)
{
    if (fd >= 0 && fd <= 2) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _isatty(int fd)
{
    if (fd >= 0 && fd <= 2)
        return 1;
    errno = EBADF;
    return 0;
}

/* There are no files. */
int _open(const char *path, int flags, int mode)
{
    (void)path; (void)flags; (void)mode;
    errno = ENOENT;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    errno = ESPIPE;
    return -1;
}

int _link(const char *old, const char *new)
{
    (void)old; (void)new;
    errno = EMLINK;
    return -1;
}

int _unlink(const char *path)
{
    (void)path;
    errno = ENOENT;
    return -1;
}

/* There is one "process" and nothing to signal. abort() ends up here. */
int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
        /* nothing to return to */
    }
}

/*
 * Time since start-up, from the 32-bit cycle counter. The counter wraps about
 * every 107 s at 40 MHz, so each call adds the cycles since the previous call to
 * a 64-bit total; callers must ask at least that often for the time to stay right.
 */
static uint64_t uptime_us(void)
{
    static uint32_t last;
    static uint64_t cycles;
    uint32_t now = cycle_count();

    cycles += (uint32_t)(now - last);
    last = now;
    return cycles / CYCLES_PER_US;
}

/* time() and gettimeofday(): seconds since start-up (there is no real-time clock). */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    uint64_t us = uptime_us();

    if (tv) {
        tv->tv_sec = (time_t)(us / 1000000u);
        tv->tv_usec = (suseconds_t)(us % 1000000u);
    }
    return 0;
}

/* clock(): newlib adds these fields up and treats the result as CLOCKS_PER_SEC ticks. */
clock_t _times(struct tms *t)
{
    clock_t ticks = (clock_t)(uptime_us() / (1000000u / CLOCKS_PER_SEC));

    if (t) {
        t->tms_utime = ticks;
        t->tms_stime = 0;
        t->tms_cutime = 0;
        t->tms_cstime = 0;
    }
    return ticks;
}
