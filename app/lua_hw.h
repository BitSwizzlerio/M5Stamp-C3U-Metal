/*
 * lua_hw.h - Lua functions for the C3U's LED and button.
 */
#ifndef LUA_HW_H
#define LUA_HW_H

#include "lua.h"

void lua_hw_open(lua_State *L);             /* set up the pins and add led, button, delay, millis */

#endif
