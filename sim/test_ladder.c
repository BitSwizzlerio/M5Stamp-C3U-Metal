/*
 * test_ladder.c - the ladder engine's tests, on a PC (sim/build.py test).
 *
 * The engine (app/plc.c, plc_asm.c, plc_show.c) is plain C, so it runs here on
 * the simulated chip in sim/hw.c, where a test can press the button, move the
 * clock on and look at every pin. Four groups:
 *   1. every program in examples/ladder assembles, and draws as it did before;
 *   2. every mistake gets its message;
 *   3. programs behave: the motor latches, the tank fills, timers and counters
 *      count, numbers add up;
 *   4. thousands of mangled programs neither crash nor overrun anything. On the
 *      PC that means "doesn't crash"; in GitHub Actions the same test runs under
 *      AddressSanitizer, which stops at the first byte read or written out of
 *      bounds.
 * Everything printed is compared with sim/expected.txt, so a drawing that
 * changes shows up as a difference. "build.py test --update" accepts it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "plc.h"
#include "sim.h"

static int passed, failed;
static void *workspace;                         /* for plc_show() */

static void check(int ok, const char *what, ...)
{
    va_list args;
    va_start(args, what);
    printf(ok ? "PASS  " : "FAIL  ");
    vprintf(what, args);
    printf("\n");
    va_end(args);
    if (ok)
        passed++;
    else
        failed++;
}

/* Load IL as plc.load() does. Returns 1, or 0 with the message in 'error'. */
static int load(const char *text, char *error, size_t size)
{
    static struct plc_program prog;
    if (!plc_assemble(text, strlen(text), &prog, error, size))
        return 0;
    plc_install(&prog, text, strlen(text));
    return 1;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    static char buf[PLC_SOURCE_MAX + 1];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    for (char *p = buf; (p = strchr(p, '\r')) != NULL;)
        memmove(p, p + 1, strlen(p));           /* a Windows checkout's CRLF */
    return buf;
}

/* Move the clock on, polling every millisecond as the console's idle loop would. */
static void run_ms(unsigned ms)
{
    while (ms-- > 0) {
        sim_advance_us(1000);
        sim_settle();
        plc_poll();
        sim_settle();
    }
}

static int pin(int n)        { return (sim_pins() >> n) & 1; }
static int m_bit(int n)      { return (plc_image.m[n / 32] >> (n % 32)) & 1; }
static void press(unsigned ms)                  /* the board's button, G9, held for ms */
{
    sim_set_input(9, 0);
    run_ms(ms);
    sim_set_input(9, -1);
    run_ms(50);
}
static void command(uint32_t word, uint32_t value) /* as the viewer writes it over JTAG */
{
    plc_image.command_value = value;
    plc_image.command = word;
    run_ms(5);
}

static void fresh(void)
{
    sim_reset();
    plc_init();
}

/* ---- 1. the examples, drawn ------------------------------------------------------- */
static void examples(const char *dir)
{
    static const char *const names[] = { "motor", "flasher", "numbers", "toggle", "tank" };
    char path[512], error[200];

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(path, sizeof path, "%s/%s.il", dir, names[i]);
        char *text = read_file(path);
        fresh();
        int ok = text && load(text, error, sizeof error);
        check(ok, "examples/ladder/%s.il assembles%s%s", names[i], ok ? "" : ": ", ok ? "" : (text ? error : "can't read it"));
        if (!ok)
            continue;
        plc_run(10000);
        run_ms(100);
        printf("--- %s.il after 100 ms\n", names[i]);
        plc_show(workspace, false);
    }
}

/* ---- 2. mistakes ---------------------------------------------------------------- */
static void mistakes(void)
{
    static const struct { const char *il, *says; } cases[] = {
        { "LD M0\nLDD G4", "line 2: unknown instruction 'LDD'" },
        { "LD M0\nOUT G9", "G9 is the button" },
        { "LD G12\nOUT G4", "flash chip" },
        { "LD G18\nOUT G4", "USB port" },
        { "LD M0\nOUT G2", "RGB LED" },
        { "LD M0\nAND G10", "the last rung has no coil" },
        { "AND M0\nOUT M1", "nothing to join to" },
        { "LD M0\nOUT M1\nOUT M1", "already has a coil" },
        { "LD M0\nLD M1\nOUT M2", "blocks are still open" },
        { "LD M0\nORB\nOUT M1", "needs two blocks" },
        { "LD M0\nTON T0\nOUT M1", "needs a time in ms" },
        { "LD M0\nTON T0 0\nOUT M1", "must be 1 to" },
        { "LD M0\nTON C0 10\nOUT M1", "needs a timer" },
        { "LD M0\nCTU C0 5\nOUT M2\nLD M1\nCTD C0 6\nOUT M3", "give them the same count" },
        { "LD M0\nMOV 5 7", "has to go into a register" },
        { "LD D0\nOUT M1", "holds a number, not a bit" },
        { "LD> G4 5\nOUT M1", "is a bit, not a number" },
        { "LD M0\nDIV D0 0 D1", "DIV by 0" },
        { "LD M0\nADD D0 1", "needs 3 values" },
        { "LD M0\nMOV 999999999 D0", "is too big" },
        { "LD=< D0 1\nOUT M1", "compare contacts are LD=" },
        { "LD M300\nOUT M1", "isn't something the ladder knows" },
        { "LD M0 M1\nOUT M2", "unexpected 'M1'" },
    };
    char error[200];

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fresh();
        int ok = load(cases[i].il, error, sizeof error);
        check(!ok && strstr(error, cases[i].says) != NULL, "a mistake is refused: \"%s\"", cases[i].says);
        if (ok || !strstr(error, cases[i].says))
            printf("      got: %s\n", ok ? "(loaded)" : error);
    }
}

/* ---- 3. behaviour ---------------------------------------------------------------- */
static void behaviour(const char *dir)
{
    char path[512], error[200];

    /* The motor: the button latches G4, the fan follows 2 s later, Stop on the panel drops both. */
    snprintf(path, sizeof path, "%s/motor.il", dir);
    fresh();
    load(read_file(path), error, sizeof error);
    plc_run(10000);
    run_ms(50);
    check(!pin(4), "motor: G4 off at the start");
    press(100);
    check(pin(4), "motor: a press latches G4 on");
    run_ms(1500);
    check(!pin(5), "motor: the fan waits");
    run_ms(600);
    check(pin(5), "motor: the fan is on after 2 s");
    command(PLC_CMD_SET_M | (1u | 0x100u) << 8, 0);            /* Stop on the panel: M1 = 1 ... */
    run_ms(30);                                                 /* (held for longer than a scan) */
    command(PLC_CMD_SET_M | 1u << 8, 0);                       /* ... and let go */
    run_ms(20);
    check(!pin(4) && !pin(5), "motor: Stop on the panel drops the motor and the fan");

    /* A press shorter than a scan can be missed; longer ones never are. */
    int seen = 0;
    for (int i = 0; i < 20; i++) {
        command(PLC_CMD_SET_M | 1u << 8, 0);
        fresh();
        load(read_file(path), error, sizeof error);
        plc_run(10000);
        run_ms(3 + (unsigned)i);                               /* start the press at a different point in the scan */
        sim_set_input(9, 0);
        run_ms(12);
        sim_set_input(9, -1);
        run_ms(20);
        seen += pin(4);
    }
    check(seen == 20, "motor: every 12 ms press is caught by a 10 ms scan (%d of 20)", seen);

    /* The tank: Auto fills it towards the setpoint at about 5 a second. */
    snprintf(path, sizeof path, "%s/tank.il", dir);
    fresh();
    load(read_file(path), error, sizeof error);
    plc_run(10000);
    run_ms(50);
    check(plc_image.d[1] == 60, "tank: the setpoint starts at 60 (%d)", plc_image.d[1]);
    command(PLC_CMD_SET_M | (2u | 0x100u) << 8, 0);            /* Auto on */
    run_ms(2000);
    check(pin(4) && plc_image.d[0] >= 9 && plc_image.d[0] <= 11, "tank: the pump fills about 10 in 2 s (%d)", plc_image.d[0]);
    command(PLC_CMD_SET_D | 1u << 8, 12);                      /* Fill to 12 */
    run_ms(2000);
    check(!pin(4) && plc_image.d[0] == 12, "tank: the pump stops at the setpoint (%d)", plc_image.d[0]);

    /* Timers, counters and numbers, as the board's self-test checks them. */
    fresh();
    load("LD M0\nTP T0 100\nOUT M2", error, sizeof error);
    plc_run(10000);
    command(PLC_CMD_SET_M | (0u | 0x100u) << 8, 0);
    run_ms(20);
    int a = m_bit(2);
    command(PLC_CMD_SET_M | 0u << 8, 0);
    run_ms(40);
    int b = m_bit(2);
    run_ms(80);
    check(a && b && !m_bit(2), "TP: a 100 ms pulse, even after the rung goes off");

    fresh();
    load("LD M0\nCTD C0 3\nOUT M2", error, sizeof error);
    plc_run(10000);
    int start = (int)plc_image.c[0].count;
    for (int i = 0; i < 3; i++) {
        command(PLC_CMD_SET_M | (0u | 0x100u) << 8, 0);
        run_ms(20);
        command(PLC_CMD_SET_M | 0u << 8, 0);
        run_ms(20);
    }
    check(start == 3 && plc_image.c[0].count == 0 && m_bit(2), "CTD: counts down from 3, done at 0");

    fresh();
    load("LD M0\nADD D0 5 D0\nLD>= D0 10\nOUT M1\nLD M3\nDIV D2 -3 D4", error, sizeof error);
    plc_step();
    command(PLC_CMD_SET_M | (0u | 0x100u) << 8, 0);
    plc_step();
    plc_step();
    command(PLC_CMD_SET_D | 2u << 8, (uint32_t)-7);
    command(PLC_CMD_SET_M | (3u | 0x100u) << 8, 0);
    plc_step();
    /* Three scans with M0 on add 5 three times; -7 / -3 rounds towards 0. */
    check(plc_image.d[0] == 15 && m_bit(1) && plc_image.d[4] == 2, "ADD, LD>= and DIV (%d, %d, %d)", plc_image.d[0], m_bit(1), plc_image.d[4]);
}

/* ---- 4. mangled programs ------------------------------------------------------------- */
/* Send stdout to nowhere (1) and back (0): thousands of drawings aren't worth keeping. */
static void quiet(int on)
{
    static int saved = -1;
    fflush(stdout);
    if (on) {
        saved = dup(fileno(stdout));
        int nowhere = open(
#ifdef _WIN32
            "NUL",
#else
            "/dev/null",
#endif
            O_WRONLY);
        dup2(nowhere, fileno(stdout));
        close(nowhere);
    } else if (saved >= 0) {
        dup2(saved, fileno(stdout));
        close(saved);
        saved = -1;
    }
}

static uint32_t seed = 12345;
static uint32_t rnd(uint32_t n) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % n; }

static void robustness(void)
{
    static const char *const words[] = {
        "LD", "LDN", "LDP", "LDF", "AND", "ANDN", "OR", "ORN", "ANB", "ORB", "OUT", "SET", "RST",
        "TON", "TOF", "TP", "RTO", "CTU", "CTD", "LD>=", "AND<", "OR<>", "MOV", "ADD", "SUB", "MUL", "DIV",
        "G4", "G5", "G9", "G10", "M0", "M1", "M255", "T0", "T31", "C0", "D0", "D63", "D64", "G99",
        "0", "1", "-5", "500", "999999999999", "]]", ";", "--", "//", "@lamp", "\t", "", "LD=", "x",
    };
    static char text[PLC_SOURCE_MAX + 64];
    char error[200];
    int loaded = 0, rounds = 3000;

    quiet(1);
    for (int r = 0; r < rounds; r++) {
        size_t n = 0;
        int lines = 1 + (int)rnd(40);
        for (int l = 0; l < lines && n < sizeof text - 64; l++) {
            int tokens = (int)rnd(5);
            for (int t = 0; t < tokens; t++)
                n += (size_t)snprintf(text + n, sizeof text - n, "%s%s", t ? " " : "", words[rnd(sizeof words / sizeof words[0])]);
            text[n++] = rnd(10) ? '\n' : '\r';
        }
        text[n] = '\0';
        fresh();
        if (load(text, error, sizeof error)) {
            loaded++;
            plc_run(1000 + rnd(20000));
            run_ms(rnd(30));
            plc_show(workspace, rnd(2));        /* printed into the void: see quiet() */
        }
    }
    quiet(0);
    check(1, "%d mangled programs: none crashed (%d of them loaded, ran and were drawn)", rounds, loaded);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "examples/ladder";

    setvbuf(stdout, NULL, _IONBF, 0);
    workspace = malloc(plc_show_workspace_size());
    examples(dir);
    mistakes();
    behaviour(dir);
    robustness();
    printf("\n%d passed, %d failed\n", passed, failed);
    free(workspace);
    return failed ? 1 : 0;
}
