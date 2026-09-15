/*
 * linker_symbols.h - addresses defined by c3u-metal.ld, for C code.
 *
 * A linker symbol is just an address: there is no variable stored there.
 * Declaring one as an array (char name[]) lets C use it as an address
 * without ever reading from it.
 */
#ifndef LINKER_SYMBOLS_H
#define LINKER_SYMBOLS_H

extern char _heap_start[], _heap_end[];         /* the heap, handed out by _sbrk() in syscalls.c */
extern char _stack_bottom[], _stack_top[];      /* the stack grows down from _stack_top */

#endif
