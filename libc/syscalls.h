/*
 * syscalls.h - the part of syscalls.c that other C files call directly.
 * (newlib calls the rest of syscalls.c itself.)
 */
#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stddef.h>

/* Move the end of the heap in use by 'increment' bytes. _sbrk(0) just returns where it is. */
void *_sbrk(ptrdiff_t increment);

#endif
