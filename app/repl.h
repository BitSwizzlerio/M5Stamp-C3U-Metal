/*
 * repl.h - an interactive Lua prompt over the USB console.
 */
#ifndef REPL_H
#define REPL_H

#include <stdbool.h>
#include "lua.h"

void repl_run(lua_State *L);                /* read, evaluate, print, forever */

/*
 * For C functions that take a while (such as delay()): raise a Lua error if
 * Ctrl-C was pressed, and keep any other keys for the next input line.
 */
void repl_check_interrupt(lua_State *L);

/* The same check, for a C function that wants to finish neatly: true if Ctrl-C was pressed. */
bool repl_ctrl_c(void);

#endif
