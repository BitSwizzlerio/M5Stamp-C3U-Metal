/*
 * lua_hw.c - Lua functions for the C3U's hardware.
 *
 *   led(r, g, b)              set the RGB LED; each 0-255, and led(0, 0, 0) turns it off
 *   button()                  true while the button is held
 *   delay(ms)                 wait this many milliseconds (Ctrl-C still stops it)
 *   millis()                  milliseconds since start-up (wraps after about 24 days)
 *   micros()                  microseconds since start-up (wraps after about 36 minutes)
 *
 *   gpio.output(pin)          make a pin an output, starting low
 *   gpio.input(pin [, pull])  make a pin an input; pull is "up", "down" or "none" (the default)
 *   gpio.write(pin, level)    level is 1 or 0 (true and false work too)
 *   gpio.read(pin)            1 or 0. In Lua 0 counts as true, so write
 *                             "if gpio.read(4) == 1 then", not "if gpio.read(4) then".
 *
 *   adc(pin [, atten])        the voltage on GPIO0, 1, 3 or 4 as 0-4095; atten 0-3 sets the
 *                             range: 4095 is about 0.8 V (0) to about 2.9 V (3, the default)
 *
 * gpio.output() and gpio.input() refuse the button's pin and the LED's pin;
 * button() and led() look after those. gpio.read() works on any usable pin.
 *
 * Each is an ordinary C function with Lua's calling convention: arguments are
 * read from the Lua stack, results are pushed onto it, and the return value is
 * how many results there are.
 *
 * Read first: drivers/gpio.h, drivers/sk6812.h and app/repl.h.
 * Try this:   add led_hsv(h, s, v) for rainbow colours (exercise 1).
 */
#include <stdbool.h>
#include <stdint.h>
#include "adc.h"
#include "board.h"
#include "gpio.h"
#include "lua.h"
#include "lauxlib.h"
#include "lua_hw.h"
#include "repl.h"
#include "sk6812.h"
#include "uptime.h"


/* ---- led, button, delay, millis ----------------------------------------- */

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

    sk6812_send_grb((g << 16) | (r << 8) | b);     /* the LED wants green, red, blue */
    return 0;
}

/* button() -> boolean */
static int l_button(lua_State *L)
{
    lua_pushboolean(L, !gpio_read(BTN_PIN));    /* pulled up, so low means pressed */
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

/*
 * micros() -> integer
 *
 * A Lua integer here is 32 bits (LUA_32BITS in CMakeLists.txt), so this counts up to
 * 2^31 microseconds and starts again: about 36 minutes, against 24 days for millis().
 * A difference stays right across the wrap if it is masked the same way:
 * (micros() - t) & 0x7FFFFFFF.
 */
static int l_micros(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(uptime_us() & (uint64_t)LUA_MAXINTEGER));
    return 1;
}


/* ---- gpio ----------------------------------------------------------------- */

/* Read argument 'arg' as a pin number, or raise a Lua error saying why that pin can't be used. */
static int check_pin(lua_State *L, int arg)
{
    int pin = (int)luaL_checkinteger(L, arg);
    const char *problem = gpio_check_pin(pin);

    if (problem != NULL)
        luaL_argerror(L, arg, problem);
    return pin;
}

/*
 * Read argument 'arg' as a pin that Lua may reconfigure: any usable pin except
 * the button's and the LED's. The button connects its pin to ground, so driving
 * that pin high while it is pressed would short the output. Changing the LED's
 * pin would stop led() working until the board restarts. Reading either is fine.
 */
static int check_free_pin(lua_State *L, int arg)
{
    int pin = check_pin(L, arg);

    luaL_argcheck(L, pin != BTN_PIN, arg, "that pin is the button; driving it could short it to ground");
    luaL_argcheck(L, pin != LED_PIN, arg, "that pin drives the RGB LED; use led()");
    return pin;
}

/* gpio.output(pin) */
static int l_gpio_output(lua_State *L)
{
    gpio_output(check_free_pin(L, 1));
    return 0;
}

/* gpio.input(pin [, pull]) */
static int l_gpio_input(lua_State *L)
{
    static const char *const names[] = { "none", "up", "down", NULL };
    static const enum gpio_pull pulls[] = { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };
    int pin = check_free_pin(L, 1);

    gpio_input(pin, pulls[luaL_checkoption(L, 2, "none", names)]);
    return 0;
}

/* gpio.write(pin, level) */
static int l_gpio_write(lua_State *L)
{
    int pin = check_pin(L, 1);
    bool high;

    if (lua_isboolean(L, 2)) {
        high = lua_toboolean(L, 2);
    } else {
        lua_Integer level = luaL_checkinteger(L, 2);
        luaL_argcheck(L, level == 0 || level == 1, 2, "must be 1 or 0");
        high = (level == 1);
    }
    gpio_write(pin, high);
    return 0;
}

/* gpio.read(pin) -> 1 or 0 */
static int l_gpio_read(lua_State *L)
{
    lua_pushinteger(L, gpio_read(check_pin(L, 1)) ? 1 : 0);
    return 1;
}

static const luaL_Reg gpio_functions[] = {
    { "output", l_gpio_output },
    { "input",  l_gpio_input },
    { "write",  l_gpio_write },
    { "read",   l_gpio_read },
    { NULL,     NULL },
};


/* ---- adc (exercise 11) ------------------------------------------------------ */

/* adc(pin [, atten]) -> 0-4095 */
static int l_adc(lua_State *L)
{
    int pin = check_pin(L, 1);
    lua_Integer atten = luaL_optinteger(L, 2, 3);
    int value;

    luaL_argcheck(L, pin <= ADC_MAX_PIN, 1, "only GPIO0 to GPIO4 can be read by the ADC");
    luaL_argcheck(L, pin != LED_PIN, 1, "that pin drives the RGB LED; use led()");
    luaL_argcheck(L, atten >= 0 && atten <= 3, 2, "must be 0-3");
    value = adc_read(pin, (int)atten);
    if (value == ADC_FAILED)
        return luaL_error(L, "the ADC did not finish its conversion");
    lua_pushinteger(L, value);
    return 1;
}

void lua_hw_open(lua_State *L)
{
    sk6812_init();                              /* LED pin as an output, LED off */
    gpio_input(BTN_PIN, GPIO_PULL_UP);
    adc_init();                                 /* the ADC on, ready to read */

    lua_register(L, "led", l_led);
    lua_register(L, "button", l_button);
    lua_register(L, "delay", l_delay);
    lua_register(L, "millis", l_millis);
    lua_register(L, "micros", l_micros);
    lua_register(L, "adc", l_adc);

    luaL_newlib(L, gpio_functions);             /* a new table holding the gpio functions ... */
    lua_setglobal(L, "gpio");                   /* ... stored in the global variable "gpio" */
}
