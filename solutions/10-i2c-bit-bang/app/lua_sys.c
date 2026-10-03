/*
 * lua_sys.c - Lua functions about the system itself.
 *
 *   help()          list the functions this board adds to Lua
 *   mem()           show how the heap and the stack are being used
 *   peek(address)   read the 32-bit value stored at any address
 *   poke(addr, val) write 32 bits to any address: the other half of peek()
 *   hex(value)      a number as "0x" and 8 hex digits, e.g. hex(peek(0x6000403C))
 *   crash()         crash on purpose, to see the crash report (app/trap_report.c)
 *   reset()         start the program again, without unplugging the board
 *
 * peek(0) does not crash: nothing protects address 0 on this chip, so reading
 * it just returns whatever is there. A program that reads a NULL pointer here
 * carries on with a wrong value instead of stopping.
 *
 * poke() can stop the board, because a hardware register is only an address:
 * writing to the ones that set the clock, or to the ones for the pins the flash
 * chip uses, ends the program. reset(), or unplugging the board, brings it back.
 *
 * Try this: mem(), then t = {} for i = 1, 10000 do t[i] = i end mem(), then
 *           t = nil collectgarbage() mem(). Where did the memory go?
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "esp32c3-regs.h"
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
    "  micros()                   microseconds since start-up (wraps after 36 minutes)\n"
    "  gpio.output(pin)           make a pin an output (it starts low)\n"
    "  gpio.input(pin [, pull])   make a pin an input; pull is \"up\", \"down\" or \"none\"\n"
    "  gpio.write(pin, level)     set an output to 1 or 0\n"
    "  gpio.read(pin)             1 or 0 (in Lua 0 counts as true, so compare: == 1)\n"
    "  i2c.setup(sda, scl)        use two pins as an I2C bus; a third argument sets the top kHz\n"
    "  i2c.scan()                 the addresses of the devices that answer\n"
    "  i2c.write(addr, bytes)     send a string of bytes; true if the device answered\n"
    "  i2c.read(addr, n)          read n bytes as a string, or nil\n"
    "  i2c.writeread(addr, b, n)  send b, then read n bytes without letting go of the bus\n"
    "  mem()                      show memory use\n"
    "  peek(address)              read 32 bits from memory, e.g. hex(peek(0x6000403C))\n"
    "  poke(address, value)       write 32 bits to memory: the other half of peek()\n"
    "  hex(value)                 write a number in hexadecimal\n"
    "  crash()                    crash on purpose, to see the crash report\n"
    "  reset()                    start the program again, without unplugging the board\n"
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
 * poke(address, value) -> nothing
 *
 * Two things are refused. An address that is not a multiple of 4, because the CPU
 * writes 32 bits at a time and an unaligned store traps; and the USB console's own
 * registers, because writing those would leave the board unable to say what went
 * wrong. Everything else is allowed, including addresses that break the program.
 */
static int l_poke(lua_State *L)
{
    uintptr_t address = (uintptr_t)(lua_Unsigned)luaL_checkinteger(L, 1);
    uint32_t value = (uint32_t)(lua_Unsigned)luaL_checkinteger(L, 2);

    luaL_argcheck(L, (address & 3) == 0, 1, "must be a multiple of 4: the CPU writes 32 bits at a time");
    luaL_argcheck(L, (address & ~0xFFFu) != USB_SERIAL_BLOCK, 1,
                  "those are the USB console's registers, and the board talks through them");
    *(volatile uint32_t *)address = value;
    return 0;
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

/*
 * reset(): start the program again, as if the board had been unplugged and plugged
 * back in. Only the CPU is reset, so the USB console keeps its connection and the
 * terminal stays open. The rest of the chip keeps whatever state it was left in,
 * until the start-up code sets it up again.
 */
static int l_reset(lua_State *L)
{
    (void)L;
    fflush(stdout);                             /* anything already printed goes out first */
    REG(RTC_CNTL_OPTIONS0) |= RTC_CNTL_SW_PROCPU_RST;
    while (REG(RTC_CNTL_OPTIONS0)) {            /* wait for the reset to arrive; reading a */
    }                                           /* register keeps the loop from being optimised away */
    return 0;
}

void lua_sys_open(lua_State *L)
{
    lua_register(L, "help", l_help);
    lua_register(L, "mem", l_mem);
    lua_register(L, "peek", l_peek);
    lua_register(L, "poke", l_poke);
    lua_register(L, "hex", l_hex);
    lua_register(L, "crash", l_crash);
    lua_register(L, "reset", l_reset);
}
