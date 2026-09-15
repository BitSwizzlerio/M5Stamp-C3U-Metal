/*
 * stack_check.h - how much of the stack has been used, and has it overflowed?
 *
 * crt0.S fills the whole stack with STACK_FILL before anything uses it. Stack
 * memory that still holds STACK_FILL has never been used. If even the bottom
 * word has changed, the stack grew past its end into the heap below it.
 *
 * Included by crt0.S too, so the C-only part is kept away from the assembler.
 */
#ifndef STACK_CHECK_H
#define STACK_CHECK_H

#define STACK_FILL  0x5741C4ED              /* any value unlikely to be pushed by chance */

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>

bool   stack_overflowed(void);              /* true if the bottom of the stack has been written to */
size_t stack_max_used(void);                /* the most stack ever in use, in bytes */
size_t stack_size(void);                    /* the size of the whole stack, in bytes */
#endif

#endif
