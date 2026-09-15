/*
 * lua_sys.h - Lua functions about the system itself: help, mem, peek, hex, crash.
 */
#ifndef LUA_SYS_H
#define LUA_SYS_H

#include "lua.h"

void lua_sys_open(lua_State *L);            /* add help, mem, peek, hex and crash */

#endif
