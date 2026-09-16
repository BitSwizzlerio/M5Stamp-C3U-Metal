/*
 * lua_hw.c - Lua functions for the C3U's hardware.
 *
 *   led(r, g, b)              set the RGB LED; each 0-255, and led(0, 0, 0) turns it off
 *   led_hsv(h, s, v)          set the LED by hue (0-359), saturation and value (0-255)
 *   button()                  true while the button is held
 *   delay(ms)                 wait this many milliseconds (Ctrl-C still stops it)
 *   millis()                  milliseconds since start-up (wraps after about 24 days)
 *
 *   gpio.output(pin)          make a pin an output, starting low
 *   gpio.input(pin [, pull])  make a pin an input; pull is "up", "down" or "none" (the default)
 *   gpio.write(pin, level)    level is 1 or 0 (true and false work too)
 *   gpio.read(pin)            1 or 0. In Lua 0 counts as true, so write
 *                             "if gpio.read(4) == 1 then", not "if gpio.read(4) then".
 *
 * Each is an ordinary C function with Lua's calling convention: arguments are
 * read from the Lua stack, results are pushed onto it, and the return value is
 * how many results there are.
 *
 * Read first: drivers/gpio.h, drivers/sk6812.h and app/repl.h.
 * Solution:   exercise 1a adds led_hsv(h, s, v).
 */
#include <stdbool.h>
#include <stdint.h>
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

/*
 * led_hsv(h, s, v): hue 0-359 is the position round the colour wheel (0 red,
 * 120 green, 240 blue); saturation and value (brightness) are 0-255.
 *
 * The wheel is split into six 60-degree pieces. In each piece one channel is
 * at v, one is at the bottom (v with the saturation taken out), and the third
 * ramps up or down between them.
 */
static int l_led_hsv(lua_State *L)
{
    lua_Integer h = luaL_checkinteger(L, 1);
    uint32_t s = channel(L, 2);
    uint32_t v = channel(L, 3);

    luaL_argcheck(L, h >= 0 && h <= 359, 1, "must be 0-359");

    uint32_t bottom = v * (255 - s) / 255;
    uint32_t ramp = (v - bottom) * (uint32_t)(h % 60) / 60;
    uint32_t r, g, b;

    switch (h / 60) {
    case 0:  r = v;             g = bottom + ramp;  b = bottom;         break;  /* red to yellow */
    case 1:  r = v - ramp;      g = v;              b = bottom;         break;  /* yellow to green */
    case 2:  r = bottom;        g = v;              b = bottom + ramp;  break;  /* green to cyan */
    case 3:  r = bottom;        g = v - ramp;       b = v;              break;  /* cyan to blue */
    case 4:  r = bottom + ramp; g = bottom;         b = v;              break;  /* blue to magenta */
    default: r = v;             g = bottom;         b = v - ramp;       break;  /* magenta to red */
    }
    sk6812_send_grb((g << 16) | (r << 8) | b);
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

/* gpio.output(pin) */
static int l_gpio_output(lua_State *L)
{
    int pin = check_pin(L, 1);

    /* The button connects its pin to ground: driving that pin high while it is pressed would short the output. */
    luaL_argcheck(L, pin != BTN_PIN, 1, "that pin is the button; driving it could short it to ground");
    gpio_output(pin);
    return 0;
}

/* gpio.input(pin [, pull]) */
static int l_gpio_input(lua_State *L)
{
    static const char *const names[] = { "none", "up", "down", NULL };
    static const enum gpio_pull pulls[] = { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };
    int pin = check_pin(L, 1);

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


void lua_hw_open(lua_State *L)
{
    sk6812_init();                              /* LED pin as an output, LED off */
    gpio_input(BTN_PIN, GPIO_PULL_UP);

    lua_register(L, "led", l_led);
    lua_register(L, "led_hsv", l_led_hsv);
    lua_register(L, "button", l_button);
    lua_register(L, "delay", l_delay);
    lua_register(L, "millis", l_millis);

    luaL_newlib(L, gpio_functions);             /* a new table holding the gpio functions ... */
    lua_setglobal(L, "gpio");                   /* ... stored in the global variable "gpio" */
}
