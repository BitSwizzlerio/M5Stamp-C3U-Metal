/*
 * main.c - C3U-Metal: start Lua and run an interactive prompt over USB.
 *
 * Open a terminal on the board's COM port and press Enter for a "> " prompt.
 *
 * Before main() runs, crt0.S (the C runtime) has set up sp and gp, called
 * SystemInit() (system_esp32c3.c: watchdogs off, CPU at 40 MHz, cycle counter
 * on), copied RAM code and .data, cleared .bss and run newlib's start-up functions.
 *
 * Read first: boot/crt0.S; afterwards app/repl.c and app/lua_hw.c.
 * Try this:   add a Lua function of your own to lua_hw.c (copy l_button and
 *             lua_register it), rebuild, flash, and call it from the console.
 */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "cpu.h"
#include "linker_symbols.h"
#include "lua_hw.h"
#include "lua_sys.h"
#include "repl.h"
#include "tick.h"
#include "usb_serial.h"

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

    /* The base library also has dofile() and loadfile(). There are no files, and
       called with no name they read Lua from the console instead, with no prompt
       and no way to finish. Take them away. */
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
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
    lua_sys_open(L);                        /* help, mem, peek, hex, crash */

    cpu_interrupts_off();                   /* the ROM leaves them on (see exercise 1c) */
    tick_start(1000);                       /* a timer interrupt every millisecond (exercise 4) */
    usb_serial_start_rx_interrupt();        /* received keys arrive by interrupt (exercise 5) */
    cpu_interrupts_on();                    /* last, once everything is ready for it */

    printf("\n%s\nC3U-Metal: %d KB heap. Type help() to see what this board adds to Lua.\n"
           "Press Enter for a prompt.\n",
           LUA_COPYRIGHT, (int)((_heap_end - _heap_start) / 1024));
    repl_run(L);
}
