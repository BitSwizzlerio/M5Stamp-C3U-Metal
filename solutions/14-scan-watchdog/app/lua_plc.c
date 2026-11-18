/*
 * lua_plc.c - the plc table: ladder logic from Lua.
 *
 *   plc.load(text)         load an IL program (see app/plc_asm.c); a running ladder carries on with it
 *   plc.run([ms])          scan by itself every ms milliseconds (1-1000, default 10)
 *   plc.stop()             stop scanning; the ladder's outputs go to 0
 *   plc.step()             one scan now
 *   plc.list()             the program, with what each instruction did in the last scan
 *   plc.show()             the program drawn as ladder rungs (app/plc_show.c)
 *   plc.watch([ms])        plc.show() redrawn in place every ms (default 200) until Ctrl-C
 *   plc.status()           running or not, and how long scans take
 *   plc.get(name)          the value of G4, M10, ...: 1 or 0. For T0: ms so far, and 1 once done.
 *                          For C0: the count, and 1 once it has reached its preset. For D0: its number.
 *   plc.set(name, value)   set an M bit (1 or 0) or a D register (a number), so Lua can tell the
 *                          ladder things
 *   plc.pull(pin, pull)    an input's pull resistor: "down" (the default), "up" or "none".
 *                          With "down", a switch wired to 3.3 V reads 1 while it's pressed;
 *                          with "up", a switch wired to GND reads 0 while it's pressed.
 *   plc.watchdog([ms])     the scan watchdog (exercise 14): if the ladder runs and no scan comes for
 *                          ms milliseconds, its outputs go off and it stops. 0 turns it off.
 *                          With no argument, returns the time. 200 ms to start with.
 *   plc.help()             this list, shorter
 *
 * Lua and the ladder share the M bits and the D registers: the ladder does the
 * logic, and Lua can do what ladder is bad at, such as printing and the RGB LED.
 *
 * Read first: app/plc.h, app/plc.c and app/lua_hw.c.
 * Try this:   load a program with a mistake in it and read the error. Every
 *             message names the line, then says how to fix it.
 */
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "board.h"
#include "lua_plc.h"
#include "plc.h"
#include "repl.h"
#include "uptime.h"

static const char help_text[] =
    "Ladder logic: a small PLC that runs beside Lua.\n"
    "  plc.load[[ ... ]]          load a program written in IL, one instruction a line\n"
    "  plc.run([ms])              scan by itself every ms milliseconds (default 10)\n"
    "  plc.stop()                 stop scanning; the ladder's outputs go to 0\n"
    "  plc.step()                 one scan now\n"
    "  plc.list()                 the program, and what each instruction did last scan\n"
    "  plc.show()                 the program drawn as ladder rungs; == is a wire with power\n"
    "  plc.watch()                plc.show() again and again, until Ctrl-C\n"
    "  plc.status()               running or not, and how long scans take\n"
    "  plc.get(\"M5\")              read a pin or a relay: 1 or 0 (a timer: ms and done; D0: a number)\n"
    "  plc.set(\"M5\", 1)           set a relay, or a register: plc.set(\"D0\", 250)\n"
    "  plc.pull(8, \"up\")          an input's pull resistor: \"down\" (the default), \"up\" or \"none\"\n"
    "  plc.watchdog(200)          outputs off if no scan comes for 200 ms; 0 turns it off\n"
    "While the ladder runs, gpio.write() and friends refuse its pins; gpio.read() still works.\n"
    "Contacts:  LD LDN AND ANDN OR ORN   (N: on while the operand is 0)\n"
    "           LDP ANDP ORP, LDF ANDF ORF   (on for one scan as it turns on / off)\n"
    "Blocks:    ANB ORB      join the last two blocks (each starts with LD)\n"
    "Timers:    TON T0 ms (on-delay)  TOF T0 ms (off-delay)  TP T0 ms (pulse)\n"
    "           RTO T0 ms (keeps its time until RST)\n"
    "Counters:  CTU C0 count (up)  CTD C0 count (down to 0; with a CTU on the same counter: up/down)\n"
    "Coils:     OUT  SET  RST   (RST also resets a timer or counter)\n"
    "Numbers:   LD= LD<> LD> LD>= LD< LD<= D0 100  (and AND>=, OR<, ...: contacts that compare)\n"
    "           MOV 100 D0   ADD D0 1 D0   SUB  MUL  DIV   (where coils go; every scan the rung is on)\n"
    "Operands:  G0-G21 the pins (a pin with a coil is an output, the rest are inputs),\n"
    "           M0-M255 relays, T0-T31 timers, C0-C31 counters, D0-D63 numbers\n"
    "Try:  plc.load[[\n"
    "      LDN  G9      ; the button: G9 reads 0 while it's held\n"
    "      OUT  G4\n"
    "      ]] plc.run()\n";

static int l_help(lua_State *L)
{
    (void)L;
    fputs(help_text, stdout);
    return 0;
}

/* "G8 G10", or "none". */
static void pin_list(uint32_t mask, char *buf, size_t size)
{
    size_t n = 0;

    buf[0] = '\0';
    for (int pin = 0; pin < PLC_PIN_COUNT; pin++)
        if (mask & (1u << pin))
            n += (size_t)snprintf(buf + n, size - n, "%sG%d", n ? " " : "", pin);
    if (n == 0)
        snprintf(buf, size, "none");
}

/* Raise a Lua error without the "stdin:1:" in front: the message already says where. */
static int plc_error(lua_State *L, const char *function, const char *message)
{
    lua_pushfstring(L, "%s: %s", function, message);
    return lua_error(L);
}

/* plc.load(text) */
static int l_load(lua_State *L)
{
    size_t len;
    const char *text = luaL_checklstring(L, 1, &len);
    /* The program is assembled into memory from Lua's heap, so a mistake leaves
       the old program as it was. Lua frees the memory itself later. */
    struct plc_program *prog = lua_newuserdatauv(L, sizeof *prog, 0);
    char error[200];
    char inputs[80], outputs[80];

    if (!plc_assemble(text, len, prog, error, sizeof error))
        return plc_error(L, "plc.load", error);
    plc_install(prog, text, len);

    pin_list(prog->in_mask, inputs, sizeof inputs);
    pin_list(prog->out_mask, outputs, sizeof outputs);
    printf("%u instruction%s in %u rung%s. inputs: %s  outputs: %s%s\n",
           prog->instructions, prog->instructions == 1 ? "" : "s",
           prog->rungs, prog->rungs == 1 ? "" : "s", inputs, outputs,
           plc_image.running ? "  (running)" : "");
    return 0;
}

static void need_program(lua_State *L, const char *function)
{
    if (plc_image.words == 0)
        plc_error(L, function, "there is no program yet: plc.load[[ ... ]] one first");
}

/* plc.run([ms]) */
static int l_run(lua_State *L)
{
    lua_Integer ms = luaL_optinteger(L, 1, 10);

    luaL_argcheck(L, ms >= 1 && ms <= 1000, 1, "must be 1-1000 ms");
    need_program(L, "plc.run");
    plc_run((uint32_t)ms * 1000u);
    printf("ladder running, a scan every %d ms\n", (int)ms);
    return 0;
}

/* plc.stop() */
static int l_stop(lua_State *L)
{
    char outputs[80];

    (void)L;
    plc_stop();
    pin_list(plc_image.out_mask, outputs, sizeof outputs);
    if (plc_image.out_mask)
        printf("ladder stopped; %s set to 0\n", outputs);
    else
        printf("ladder stopped\n");
    return 0;
}

/* plc.step() */
static int l_step(lua_State *L)
{
    need_program(L, "plc.step");
    plc_step();
    return 0;
}

/* Read argument 1 as an operand name such as "G4", "M10" or "D0". */
static void check_operand(lua_State *L, const char *function, unsigned *kind, unsigned *n)
{
    size_t len;
    const char *name = luaL_checklstring(L, 1, &len);

    if (!plc_parse_operand(name, len, kind, n)) {
        lua_pushfstring(L, "'%s' isn't something the ladder knows: use G0 to G21, M0 to M255, "
                           "T0 to T31, C0 to C31 or D0 to D63", name);
        plc_error(L, function, lua_tostring(L, -1));
    }
}

/* plc.get(name) -> 1 or 0; for a timer, ms so far and done; for a counter, count and done; for D, its number */
static int l_get(lua_State *L)
{
    const struct plc_image *p = &plc_image;
    unsigned kind, n;
    uint32_t bits;

    check_operand(L, "plc.get", &kind, &n);
    if (kind == KIND_D) {
        lua_pushinteger(L, (lua_Integer)p->d[n]);
        return 1;
    }
    if (kind == KIND_T) {
        lua_pushinteger(L, (lua_Integer)(p->t[n].elapsed_us / 1000u));
        lua_pushinteger(L, (lua_Integer)p->t[n].done);
        return 2;
    }
    if (kind == KIND_C) {
        lua_pushinteger(L, (lua_Integer)p->c[n].count);
        lua_pushinteger(L, (lua_Integer)p->c[n].done);
        return 2;
    }
    if (kind == KIND_M)
        bits = p->m[n / 32] >> (n % 32);
    else if (p->claimed)
        bits = ((p->out_mask >> n & 1u) ? p->outputs : p->inputs) >> n;
    else
        bits = gpio_read((int)n);
    lua_pushinteger(L, bits & 1u);
    return 1;
}

/* plc.set(name, value) */
static int l_set(lua_State *L)
{
    unsigned kind, n;
    bool value;

    check_operand(L, "plc.set", &kind, &n);
    if (kind == KIND_D) {
        lua_Integer v = luaL_checkinteger(L, 2);
        luaL_argcheck(L, v >= INT32_MIN && v <= INT32_MAX, 2, "must fit in 32 bits");
        plc_image.d[n] = (int32_t)v;
        return 0;
    }
    if (kind != KIND_M)
        return plc_error(L, "plc.set", "only M bits and D registers can be set: the ladder drives its own "
                                       "outputs, inputs come from the pins, and timers and counters from their boxes");
    if (lua_isboolean(L, 2)) {
        value = lua_toboolean(L, 2);
    } else {
        lua_Integer v = luaL_checkinteger(L, 2);
        luaL_argcheck(L, v == 0 || v == 1, 2, "must be 1 or 0");
        value = (v == 1);
    }
    if (value)
        plc_image.m[n / 32] |= 1u << (n % 32);
    else
        plc_image.m[n / 32] &= ~(1u << (n % 32));
    return 0;
}

/* plc.pull(pin, "up" | "down" | "none"): the pin is a number, 8, or a name, "G8" */
static int l_pull(lua_State *L)
{
    static const char *const names[] = { "none", "up", "down", NULL };
    static const enum gpio_pull pulls[] = { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };
    int pin;

    if (lua_type(L, 1) == LUA_TSTRING) {
        size_t len;
        const char *name = lua_tolstring(L, 1, &len);
        unsigned kind, index;
        luaL_argcheck(L, plc_parse_operand(name, len, &kind, &index) && kind == KIND_G, 1,
                      "must be a pin: a number such as 8, or a name such as \"G8\"");
        pin = (int)index;
    } else {
        pin = (int)luaL_checkinteger(L, 1);
    }
    const char *problem = gpio_check_pin(pin);
    if (problem != NULL)
        luaL_argerror(L, 1, problem);
    luaL_argcheck(L, pin != BTN_PIN, 1, "that pin is the button, which needs its pull-up");
    luaL_argcheck(L, pin != LED_PIN, 1, "that pin drives the RGB LED");
    plc_set_pull(pin, pulls[luaL_checkoption(L, 2, NULL, names)]);
    return 0;
}

/* plc.watchdog([ms]) */
static int l_watchdog(lua_State *L)
{
    if (lua_isnone(L, 1)) {
        lua_pushinteger(L, (lua_Integer)plc_watchdog_ms());
        return 1;
    }
    lua_Integer ms = luaL_checkinteger(L, 1);
    luaL_argcheck(L, ms == 0 || (ms >= 20 && ms <= 60000), 1, "must be 0 (off) or 20-60000 ms");
    plc_set_watchdog((uint32_t)ms);
    return 0;
}

/* The comment on line 'line' of the program text, without the ';', or "". */
static void source_comment(unsigned line, char *buf, size_t size)
{
    const char *p = plc_image.source, *end = p + plc_image.source_len;

    buf[0] = '\0';
    for (unsigned n = 1; n < line && p < end; p++)
        if (*p == '\n')
            n++;
    const char *line_end = memchr(p, '\n', (size_t)(end - p));
    if (line_end == NULL)
        line_end = end;
    for (; p < line_end; p++) {
        size_t skip = (*p == ';') ? 1 : (p + 1 < line_end && ((p[0] == '-' && p[1] == '-') ||
                                                              (p[0] == '/' && p[1] == '/'))) ? 2 : 0;
        if (skip) {
            p += skip;
            while (p < line_end && (*p == ' ' || *p == '\t'))
                p++;
            size_t n = (size_t)(line_end - p);
            while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' '))
                n--;
            snprintf(buf, size, "%.*s", (int)n, p);
            return;
        }
    }
}

/* plc.list() */
static int l_list(lua_State *L)
{
    const struct plc_image *p = &plc_image;
    bool rung_done = true;
    unsigned rung = 0;

    need_program(L, "plc.list");
    printf("  #  instruction        on\n");
    for (unsigned i = 0; i < p->words; i += plc_op_words(PLC_OP(p->program[i]))) {
        uint32_t word = p->program[i];
        unsigned op = PLC_OP(word);
        char text[40], comment[64], live[32] = "";

        if (rung_done && plc_op_is_load(op)) {
            printf("    ; rung %u\n", rung++);
            rung_done = false;
        }
        if (plc_op_is_output(op) || plc_op_is_box(op))
            rung_done = true;

        plc_instruction_text(p->program, i, text, sizeof text);
        unsigned n = PLC_INDEX(word);
        if (plc_op_is_box(op) && PLC_KIND(word) == KIND_T)
            snprintf(live, sizeof live, "%u / %u ms", (unsigned)(p->t[n].elapsed_us / 1000u),
                     (unsigned)p->program[i + 1]);
        else if (plc_op_is_box(op))
            snprintf(live, sizeof live, "%u / %u", (unsigned)p->c[n].count, (unsigned)p->program[i + 1]);
        else if (op >= OP_MOV && op <= OP_DIV) {
            uint32_t result = p->program[i + (op == OP_MOV ? 2 : 3)];
            char name[8];
            plc_value_name(result, name, sizeof name);
            snprintf(live, sizeof live, "%s = %ld", name, (long)p->d[PLC_VALUE_INDEX(result)]);
        }
        source_comment(PLC_LINE(word), comment, sizeof comment);
        char on = p->scans == 0 ? '-' : ((p->trace[i / 32] >> (i % 32)) & 1u) ? '1' : '0';
        printf("%3u  %-18s %c", i, text, on);
        if (comment[0])
            printf("   %-16s ; %s", live, comment);
        else if (live[0])
            printf("   %s", live);
        printf("\n");
    }
    if (p->running)
        printf("running: a scan every %u ms, %u scan%s so far\n", (unsigned)(p->period_us / 1000),
               (unsigned)p->scans, p->scans == 1 ? "" : "s");
    else
        printf("stopped, %u scan%s so far\n", (unsigned)p->scans, p->scans == 1 ? "" : "s");
    return 0;
}

/* plc.show() */
static int l_show(lua_State *L)
{
    need_program(L, "plc.show");
    plc_show(lua_newuserdatauv(L, plc_show_workspace_size(), 0), true);
    return 0;
}

/*
 * plc.watch([ms]): draw the rungs again every ms milliseconds (default 200),
 * in the same place, until Ctrl-C. "ESC [ n A" moves the cursor up n lines and
 * "ESC [ K" clears the rest of a line: ANSI codes that nearly every terminal knows.
 */
static int l_watch(lua_State *L)
{
    lua_Integer ms = luaL_optinteger(L, 1, 200);
    unsigned lines = 0;

    luaL_argcheck(L, ms >= 50 && ms <= 10000, 1, "must be 50-10000 ms");
    need_program(L, "plc.watch");
    void *workspace = lua_newuserdatauv(L, plc_show_workspace_size(), 0);
    for (;;) {
        if (lines > 0)
            printf("\x1b[%uA", lines);
        lines = plc_show(workspace, true);
        printf("\x1b[K%s, %u scans. Ctrl-C stops watching; the ladder keeps running.\x1b[K\n",
               plc_image.running ? "running" : "stopped", (unsigned)plc_image.scans);
        lines++;
        fflush(stdout);
        uint64_t end = uptime_cycles() + (uint64_t)ms * CPU_CYCLES_PER_MS;
        while (uptime_cycles() < end)
            if (repl_ctrl_c())
                return 0;
    }
}

/* plc.status() */
static int l_status(lua_State *L)
{
    const struct plc_image *p = &plc_image;
    char inputs[80], outputs[80];

    (void)L;
    if (p->words == 0) {
        printf("no program loaded\n");
        return 0;
    }
    pin_list(p->in_mask, inputs, sizeof inputs);
    pin_list(p->out_mask, outputs, sizeof outputs);
    if (p->running)
        printf("running, a scan every %u ms\n", (unsigned)(p->period_us / 1000));
    else if (plc_fault_ms())
        printf("stopped by the watchdog, after %u ms without a scan\n", (unsigned)plc_fault_ms());
    else
        printf("stopped\n");
    if (plc_watchdog_ms())
        printf("watchdog: outputs off after %u ms without a scan\n", (unsigned)plc_watchdog_ms());
    else
        printf("watchdog: off\n");
    printf("%u scans; the last took %u us, the longest %u us; %u started late\n",
           (unsigned)p->scans, (unsigned)p->scan_us, (unsigned)p->scan_us_max, (unsigned)p->late);
    printf("inputs: %s  outputs: %s\n", inputs, outputs);
    return 0;
}

static const luaL_Reg plc_functions[] = {
    { "load",   l_load },
    { "run",    l_run },
    { "stop",   l_stop },
    { "step",   l_step },
    { "list",   l_list },
    { "show",   l_show },
    { "watch",  l_watch },
    { "status", l_status },
    { "get",    l_get },
    { "set",    l_set },
    { "pull",   l_pull },
    { "watchdog", l_watchdog },
    { "help",   l_help },
    { NULL,     NULL },
};

void lua_plc_open(lua_State *L)
{
    plc_init();
    luaL_newlib(L, plc_functions);
    lua_setglobal(L, "plc");
}
