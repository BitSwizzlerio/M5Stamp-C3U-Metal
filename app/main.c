/*
 * main.c - C3U-Metal: start Lua and run an interactive prompt over USB.
 *
 * Open a terminal on the board's COM port and press Enter for a "> " prompt.
 *
 * Before main() runs, crt0.S (the C runtime) has set up sp and gp, called
 * SystemInit() (system_esp32c3.c: watchdogs off, CPU at 40 MHz, cycle counter
 * on), copied RAM code and .data, cleared .bss and run newlib's start-up functions.
 */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "linker_symbols.h"
#include "lua_hw.h"
#include "repl.h"

/* Create a Lua state with only the libraries that make sense without files or an OS. */
static lua_State *lua_start(void)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME,       luaopen_base },
        { LUA_COLIBNAME,   luaopen_coroutine },
        { LUA_TABLIBNAME,  luaopen_table },
        { LUA_STRLIBNAME,  luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    lua_State *L = luaL_newstate();

    if (L == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        luaL_requiref(L, libs[i].name, libs[i].func, 1);    /* open it and make it a global */
        lua_pop(L, 1);
    }
    return L;
}

int main(void)
{
    lua_State *L = lua_start();

    if (L == NULL) {
        printf("C3U-Metal: not enough memory to start Lua\n");
        for (;;) {
        }
    }
    lua_hw_open(L);                         /* led, button, delay, millis, gpio */

    printf("\n%s\nC3U-Metal: %d KB heap. Hardware: led(r, g, b)  button()  delay(ms)  millis()  gpio.*\n"
           "Press Enter for a prompt.\n",
           LUA_COPYRIGHT, (int)((_heap_end - _heap_start) / 1024));
    repl_run(L);
}
