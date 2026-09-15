/*
 * stack_check.c - how much of the stack has been used, and has it overflowed?
 * See stack_check.h.
 *
 * Try this: mem(), then load("return " .. string.rep("(", 100) .. "1" .. string.rep(")", 100))
 *           and mem() again. Lua's parser calls itself once for each bracket,
 *           and every call takes stack space.
 */
#include <stdint.h>
#include "linker_symbols.h"
#include "stack_check.h"

bool stack_overflowed(void)
{
    return *(uint32_t *)_stack_bottom != STACK_FILL;
}

size_t stack_max_used(void)
{
    uint32_t *p = (uint32_t *)_stack_bottom;

    /* The stack grows down, so skip up past the words that were never used. */
    while (p < (uint32_t *)_stack_top && *p == STACK_FILL)
        p++;
    return (size_t)((char *)_stack_top - (char *)p);
}

size_t stack_size(void)
{
    return (size_t)(_stack_top - _stack_bottom);
}
