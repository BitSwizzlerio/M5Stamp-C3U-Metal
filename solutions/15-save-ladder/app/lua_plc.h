/*
 * lua_plc.h - the plc table: ladder logic from Lua.
 */
#ifndef LUA_PLC_H
#define LUA_PLC_H

#include "lua.h"

void lua_plc_open(lua_State *L);            /* add the plc table */

/* Exercise 15: load and start the ladder kept by plc.save(), if there is one. At start-up. */
void lua_plc_start_saved(lua_State *L);

#endif
