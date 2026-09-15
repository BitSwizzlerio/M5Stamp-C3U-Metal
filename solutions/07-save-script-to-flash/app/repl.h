/*
 * repl.h - an interactive Lua prompt over the USB console.
 */
#ifndef REPL_H
#define REPL_H

#include "lua.h"

void repl_run(lua_State *L);                /* read, evaluate, print, forever */

/* Run a piece of Lua source, such as a saved script, with Ctrl-C and tracebacks as for typed lines (exercise 7). */
void repl_run_chunk(lua_State *L, const char *code, size_t len, const char *name);

/*
 * For C functions that take a while (such as delay()): raise a Lua error if
 * Ctrl-C was pressed, and keep any other keys for the next input line.
 */
void repl_check_interrupt(lua_State *L);

#endif
