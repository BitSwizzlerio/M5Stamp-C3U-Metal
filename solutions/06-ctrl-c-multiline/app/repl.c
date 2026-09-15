/*
 * repl.c - an interactive Lua prompt (read-evaluate-print loop) over the USB console.
 *
 * The Lua side follows the official interpreter, lua.c from Lua 5.5.1:
 *   - each line is first tried as an expression, "return <line>;", so its value prints;
 *   - otherwise it is compiled as a statement, and if it stops too early (the syntax
 *     error ends in "<eof>") more lines are read with the ">> " prompt;
 *   - results are printed with Lua's own print(); errors print with a traceback.
 *
 * The terminal side is ours: echo, Backspace, CR/LF handling, and Ctrl-C to stop
 * Lua code that runs too long.
 *
 * Read first: app/main.c. The Lua C API used here is described in chapter 4
 *             of third_party/lua/doc/manual.html.
 * Solution:   exercise 6. Ctrl-C at the ">>" prompt cancels the whole
 *             unfinished statement.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "repl.h"
#include "stack_check.h"
#include "uptime.h"
#include "usb_serial.h"

#define PROMPT          "> "
#define PROMPT2         ">> "
#define MAX_LINE        512                 /* same as lua.c's LUA_MAXINPUT */
#define EOFMARK         "<eof>"
#define CTRL_C          0x03
#define ESC             0x1B
#define HOOK_EVERY      1000                /* Lua instructions between Ctrl-C checks */
#define STATUS_CANCELLED (-1)               /* from load_line(): Ctrl-C, so nothing to run. Lua's statuses are 0 and up. */


/* ---- Keyboard input ---------------------------------------------------- */

/* Keys typed while Lua code was running, saved by the Ctrl-C check for read_line(). */
static char typed_ahead[64];
static unsigned ahead_first, ahead_count;

static void keep_key(int c)
{
    if (ahead_count < sizeof typed_ahead) {
        typed_ahead[(ahead_first + ahead_count) % sizeof typed_ahead] = (char)c;
        ahead_count++;
    }
}

static int next_key(void)
{
    if (ahead_count > 0) {
        int c = (unsigned char)typed_ahead[ahead_first];
        ahead_first = (ahead_first + 1) % sizeof typed_ahead;
        ahead_count--;
        return c;
    }
    return usb_serial_getc();               /* -1 if nothing waiting */
}

/*
 * Read one line with echo and Backspace into buf (always 0-terminated), and its
 * length into *length. Returns false if Ctrl-C cancelled it.
 */
static bool read_line(char *buf, size_t size, size_t *length)
{
    static int previous;                    /* last key, so that CR followed by LF is one Enter */
    size_t len = 0;
    int escape = 0;                         /* 1 after ESC, 2 inside "ESC [ ..." (arrow keys etc.) */

    for (;;) {
        int c = next_key();
        if (c < 0) {
            uptime_cycles();                /* keep the uptime count right while waiting */
            continue;
        }
        int prev = previous;
        previous = c;

        if (escape == 1) {                  /* skip escape sequences instead of typing them */
            escape = (c == '[') ? 2 : 0;
            continue;
        }
        if (escape == 2) {
            if (c >= 0x40 && c <= 0x7E)     /* the final byte of the sequence */
                escape = 0;
            continue;
        }
        if (c == ESC) {
            escape = 1;
            continue;
        }

        if (c == '\n' && prev == '\r')
            continue;                       /* LF right after CR: same Enter */
        if (c == '\r' || c == '\n') {
            usb_serial_write("\n", 1);
            buf[len] = '\0';
            *length = len;
            return true;
        }
        if (c == '\b' || c == 0x7F) {       /* Backspace or Delete */
            if (len > 0) {
                len--;
                usb_serial_write("\b \b", 3);
            }
            continue;
        }
        if (c == CTRL_C) {                  /* cancel the line, and the statement it belongs to */
            usb_serial_write("^C\n", 3);
            buf[0] = '\0';
            *length = 0;
            return false;
        }
        if (c < ' ')
            continue;                       /* other control keys */
        if (len + 1 < size) {
            buf[len++] = (char)c;
            usb_serial_putc((char)c);
            usb_serial_flush();
        }
    }
}


/* ---- Reading Lua (following lua.c) ------------------------------------ */

/* Show a prompt, read a line and push it onto the Lua stack. On Ctrl-C, push nothing and return false. */
static bool push_line(lua_State *L, const char *prompt)
{
    char buf[MAX_LINE];
    size_t len;

    fflush(stdout);                         /* anything Lua printed comes before the prompt */
    usb_serial_write(prompt, strlen(prompt));
    if (!read_line(buf, sizeof buf, &len))
        return false;
    lua_pushlstring(L, buf, len);
    return true;
}

/* True if 'status' is a syntax error whose message ends in "<eof>": the statement isn't finished. */
static int incomplete(lua_State *L, int status)
{
    if (status == LUA_ERRSYNTAX) {
        size_t len;
        const char *msg = lua_tolstring(L, -1, &len);
        size_t marklen = sizeof(EOFMARK) - 1;
        if (len >= marklen && strcmp(msg + len - marklen, EOFMARK) == 0)
            return 1;
    }
    return 0;
}

/* Try to compile the line on the stack as "return <line>;". On success the chunk replaces the line. */
static int add_return(lua_State *L)
{
    const char *line = lua_tostring(L, -1);
    const char *retline = lua_pushfstring(L, "return %s;", line);
    int status = luaL_loadbufferx(L, retline, strlen(retline), "=stdin", "t");

    if (status == LUA_OK)
        lua_remove(L, -2);                  /* remove the "return" version of the text */
    else
        lua_pop(L, 2);                      /* remove the error and the "return" version */
    return status;
}

/* 'local' variables only live for one chunk, which surprises people at a prompt. */
static void check_local(const char *line)
{
    line += strspn(line, " \t");
    if (strncmp(line, "local", 5) == 0 && (line[5] == ' ' || line[5] == '\t'))
        fprintf(stderr, "warning: locals do not survive across lines in interactive mode\n");
}

/* Compile the line on the stack as a statement, reading more lines while it is unfinished. */
static int multiline(lua_State *L)
{
    size_t len;
    const char *line = lua_tolstring(L, 1, &len);

    check_local(line);
    for (;;) {
        int status = luaL_loadbufferx(L, line, len, "=stdin", "t");
        if (!incomplete(L, status))
            return status;                  /* compiled, or a real error */
        lua_pop(L, 1);                      /* remove the "<eof>" error message */
        if (!push_line(L, PROMPT2))
            return STATUS_CANCELLED;        /* Ctrl-C: give up on the whole statement */
        lua_pushliteral(L, "\n");           /* join: first line, newline, next line */
        lua_insert(L, -2);
        lua_concat(L, 3);
        line = lua_tolstring(L, 1, &len);
    }
}

/* Read a complete chunk. Leaves the compiled function, or an error message, on the stack. */
static int load_line(lua_State *L)
{
    int status;

    lua_settop(L, 0);
    if (!push_line(L, PROMPT))
        return STATUS_CANCELLED;
    if ((status = add_return(L)) != LUA_OK)
        status = multiline(L);
    lua_remove(L, 1);                       /* remove the source text */
    return status;
}


/* ---- Running Lua ------------------------------------------------------ */

void repl_check_interrupt(lua_State *L)
{
    int c;

    uptime_cycles();                        /* keep the uptime count right during long runs */
    while ((c = usb_serial_getc()) >= 0) {
        if (c == CTRL_C) {
            lua_sethook(L, NULL, 0, 0);
            luaL_error(L, "interrupted!");
        }
        keep_key(c);                        /* not Ctrl-C: keep it for the next line */
    }
}

/* Called by Lua every HOOK_EVERY instructions while a chunk runs. */
static void check_ctrl_c(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    repl_check_interrupt(L);
}

/* Error handler: add a traceback to the message (same as lua.c's msghandler). */
static int message_handler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);

    if (msg == NULL) {                      /* the error isn't a string */
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

/* Call the function on the stack with the message handler and Ctrl-C checks in place. */
static int do_call(lua_State *L, int nargs, int nresults)
{
    int base = lua_gettop(L) - nargs;

    lua_pushcfunction(L, message_handler);
    lua_insert(L, base);
    lua_sethook(L, check_ctrl_c, LUA_MASKCOUNT, HOOK_EVERY);
    int status = lua_pcall(L, nargs, nresults, base);
    lua_sethook(L, NULL, 0, 0);
    lua_remove(L, base);
    return status;
}

/* Print any values left on the stack, using Lua's print(). */
static void print_results(lua_State *L)
{
    int n = lua_gettop(L);

    if (n > 0) {
        luaL_checkstack(L, LUA_MINSTACK, "too many results to print");
        lua_getglobal(L, "print");
        lua_insert(L, 1);
        if (lua_pcall(L, n, 0, 0) != LUA_OK)
            fprintf(stderr, "error calling 'print' (%s)\n", lua_tostring(L, -1));
    }
}

static void report(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);

    fprintf(stderr, "%s\n", msg ? msg : "(error with no message)");
    lua_pop(L, 1);
}

/* Nothing stops the stack growing down into the heap, so look after every chunk (and warn once). */
static void check_stack(void)
{
    static bool warned;

    if (!warned && stack_overflowed()) {
        warned = true;
        fprintf(stderr, "warning: the stack overflowed into the heap, so memory may be damaged. "
                        "Restart the board.\n");
    }
}

void repl_run(lua_State *L)
{
    for (;;) {
        int status = load_line(L);
        if (status == STATUS_CANCELLED) {
            lua_settop(L, 0);
            continue;                       /* nothing to run, nothing to report */
        }
        if (status == LUA_OK)
            status = do_call(L, 0, LUA_MULTRET);
        if (status == LUA_OK)
            print_results(L);
        else
            report(L);
        lua_settop(L, 0);
        check_stack();
    }
}
