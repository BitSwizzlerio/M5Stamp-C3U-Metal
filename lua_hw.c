/*
 * lua_hw.c - Lua functions for the C3U's hardware.
 *
 *   led(r, g, b)   set the RGB LED; each 0-255, and led(0, 0, 0) turns it off
 *   button()       true while the button is held
 *   delay(ms)      wait this many milliseconds (Ctrl-C still stops it)
 *   millis()       milliseconds since start-up (wraps after about 24 days)
 *
 * Each is an ordinary C function with Lua's calling convention: arguments are
 * read from the Lua stack, results are pushed onto it, and the return value is
 * how many results there are.
 */
#include <stdint.h>
#include "esp32c3-regs.h"
#include "lua.h"
#include "lauxlib.h"
#include "lua_hw.h"
#include "repl.h"
#include "uptime.h"

#define LED_PIN     2
#define BTN_PIN     9

void send_grb(uint32_t grb);                /* led.S */

static void led_pin_init(void)
{
    REG(IO_MUX_GPIO2) = (REG(IO_MUX_GPIO2) & ~IO_MUX_CLEAR_MASK) | IO_MUX_MCU_SEL_GPIO;
    REG(GPIO_FUNC2_OUT_SEL_CFG) = SIG_GPIO_OUT_IDX;     /* GPIO2 follows the GPIO_OUT register */
    REG(GPIO_OUT_W1TC) = 1u << LED_PIN;                  /* output low first ...        */
    REG(GPIO_ENABLE_W1TS) = 1u << LED_PIN;               /* ... then turn the driver on */
}

static void button_pin_init(void)
{
    REG(IO_MUX_GPIO9) = (REG(IO_MUX_GPIO9) & ~IO_MUX_CLEAR_MASK)
                      | IO_MUX_MCU_SEL_GPIO | IO_MUX_FUN_IE | IO_MUX_FUN_PU;
    REG(GPIO_ENABLE_W1TC) = 1u << BTN_PIN;               /* input only */
}

/* Read argument 'arg' as a colour channel, or raise a Lua error if it isn't 0-255. */
static uint32_t channel(lua_State *L, int arg)
{
    lua_Integer value = luaL_checkinteger(L, arg);

    luaL_argcheck(L, value >= 0 && value <= 255, arg, "must be 0-255");
    return (uint32_t)value;
}

/* led(r, g, b) */
static int l_led(lua_State *L)
{
    uint32_t r = channel(L, 1);
    uint32_t g = channel(L, 2);
    uint32_t b = channel(L, 3);

    send_grb((g << 16) | (r << 8) | b);     /* the LED wants green, red, blue */
    return 0;
}

/* button() -> boolean */
static int l_button(lua_State *L)
{
    lua_pushboolean(L, (REG(GPIO_IN) & (1u << BTN_PIN)) == 0);   /* pulled up, so 0 means pressed */
    return 1;
}

/* delay(ms) */
static int l_delay(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);

    luaL_argcheck(L, ms >= 0, 1, "must not be negative");
    uint64_t end = uptime_cycles() + (uint64_t)ms * CPU_CYCLES_PER_MS;
    while (uptime_cycles() < end)
        repl_check_interrupt(L);            /* Ctrl-C raises an error and ends the wait */
    return 0;
}

/* millis() -> integer */
static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)((uptime_us() / 1000u) & (uint64_t)LUA_MAXINTEGER));
    return 1;
}

void lua_hw_open(lua_State *L)
{
    led_pin_init();
    button_pin_init();

    /* Hold the data line low for 1 ms (the LED's reset time), then switch the LED off. */
    uint64_t end = uptime_cycles() + CPU_CYCLES_PER_MS;
    while (uptime_cycles() < end) {
    }
    send_grb(0);

    lua_register(L, "led", l_led);
    lua_register(L, "button", l_button);
    lua_register(L, "delay", l_delay);
    lua_register(L, "millis", l_millis);
}
