/*
 * lua_sys.c - Lua functions about the system itself.
 *
 *   help()          list the functions this board adds to Lua
 *   mem()           show how the heap and the stack are being used
 *   peek(address)   read the 32-bit value stored at any address
 *   hex(value)      a number as "0x" and 8 hex digits, e.g. hex(peek(0x6000403C))
 *   crash()         crash on purpose, to see the crash report (app/trap_report.c)
 *   save(code)      keep a Lua script in flash; main.c runs it at start-up (exercise 7)
 *   saved()         the saved script, or nil
 *   erase()         remove the saved script
 *
 * peek(0) does not crash: nothing protects address 0 on this chip, so reading
 * it just returns whatever is there. A program that reads a NULL pointer here
 * carries on with a wrong value instead of stopping.
 *
 * Try this: mem(), then t = {} for i = 1, 10000 do t[i] = i end mem(), then
 *           t = nil collectgarbage() mem(). Where did the memory go?
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include "flash_store.h"
#include "lua.h"
#include "lauxlib.h"
#include "linker_symbols.h"
#include "lua_sys.h"
#include "stack_check.h"
#include "syscalls.h"

static const char help_text[] =
    "Functions this board adds to Lua:\n"
    "  led(r, g, b)               set the RGB LED; each colour 0-255\n"
    "  button()                   true while the button is held\n"
    "  delay(ms)                  wait this many milliseconds\n"
    "  millis()                   milliseconds since start-up\n"
    "  gpio.output(pin)           make a pin an output (it starts low)\n"
    "  gpio.input(pin [, pull])   make a pin an input; pull is \"up\", \"down\" or \"none\"\n"
    "  gpio.write(pin, level)     set an output to 1 or 0\n"
    "  gpio.read(pin)             1 or 0 (in Lua 0 counts as true, so compare: == 1)\n"
    "  mem()                      show memory use\n"
    "  peek(address)              read 32 bits from memory, e.g. hex(peek(0x6000403C))\n"
    "  hex(value)                 write a number in hexadecimal\n"
    "  crash()                    crash on purpose, to see the crash report\n"
    "  save(code)                 keep a Lua script in flash; it runs at every start-up\n"
    "  saved()                    the saved script, or nil\n"
    "  erase()                    remove the saved script\n"
    "  help()                     this list\n"
    "\n"
    "Keys: Enter runs the line, Backspace edits it, Ctrl-C stops a running program.\n"
    "Try:  for i = 1, 5 do led(0, 0, 40) delay(200) led(0, 0, 0) delay(200) end\n";

/* help() */
static int l_help(lua_State *L)
{
    (void)L;
    fputs(help_text, stdout);
    return 0;
}

/* mem() */
static int l_mem(lua_State *L)
{
    const double KB = 1024.0;
    struct mallinfo info = mallinfo();              /* newlib's malloc statistics */
    char *brk = _sbrk(0);                           /* how far malloc has claimed the heap */
    double lua_kb = lua_gc(L, LUA_GCCOUNT) + lua_gc(L, LUA_GCCOUNTB) / KB;

    printf("heap  %6.1f KB, between the variables (.bss) and the stack\n", (_heap_end - _heap_start) / KB);
    printf("      %6.1f KB  in use (Lua is using %.1f KB of it)\n", info.uordblks / KB, lua_kb);
    printf("      %6.1f KB  claimed by malloc but free again\n", info.fordblks / KB);
    printf("      %6.1f KB  never claimed\n", (_heap_end - brk) / KB);
    printf("stack %6.1f KB, at most %.1f KB used so far\n", stack_size() / KB, stack_max_used() / KB);
    if (stack_overflowed())
        printf("      the stack has OVERFLOWED into the heap: restart the board\n");
    return 0;
}

/* peek(address) -> integer */
static int l_peek(lua_State *L)
{
    uintptr_t address = (uintptr_t)(lua_Unsigned)luaL_checkinteger(L, 1);

    lua_pushinteger(L, (lua_Integer)*(volatile uint32_t *)address);
    return 1;
}

/* hex(value) -> string */
static int l_hex(lua_State *L)
{
    char text[16];

    snprintf(text, sizeof text, "0x%08lx", (unsigned long)(lua_Unsigned)luaL_checkinteger(L, 1));
    lua_pushstring(L, text);
    return 1;
}

/*
 * crash(): call a function at address 0, the same mistake as a C program calling
 * through a NULL function pointer. Address 0 holds zeros, which are not a valid
 * instruction, so the CPU traps with "illegal instruction".
 */
static int l_crash(lua_State *L)
{
    void (*volatile nowhere)(void) = NULL;      /* volatile: the compiler must really load it and call it */

    (void)L;
    nowhere();
    return 0;
}

/* save(code) -> true, or nil and the syntax error. Checking the syntax first means a typo can't be saved. */
static int l_save(lua_State *L)
{
    size_t len;
    const char *code = luaL_checklstring(L, 1, &len);

    if (len > FLASH_STORE_MAX)
        return luaL_error(L, "the script is too long: %d bytes, and at most %d fit", (int)len, FLASH_STORE_MAX);
    if (luaL_loadbufferx(L, code, len, "=saved", "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);                          /* nil, then the error message */
        return 2;
    }
    if (!flash_store_save(code, len))
        return luaL_error(L, "writing to flash failed");
    lua_pushboolean(L, 1);
    return 1;
}

/* saved() -> string or nil */
static int l_saved(lua_State *L)
{
    size_t len;
    const char *code = flash_store_load(&len);

    if (code == NULL)
        lua_pushnil(L);
    else
        lua_pushlstring(L, code, len);
    return 1;
}

/* erase() -> true */
static int l_erase(lua_State *L)
{
    if (!flash_store_erase())
        return luaL_error(L, "erasing flash failed");
    lua_pushboolean(L, 1);
    return 1;
}

void lua_sys_open(lua_State *L)
{
    lua_register(L, "save", l_save);
    lua_register(L, "saved", l_saved);
    lua_register(L, "erase", l_erase);
    lua_register(L, "help", l_help);
    lua_register(L, "mem", l_mem);
    lua_register(L, "peek", l_peek);
    lua_register(L, "hex", l_hex);
    lua_register(L, "crash", l_crash);
}
