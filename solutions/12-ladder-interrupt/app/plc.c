/*
 * plc.c - the ladder's engine: the scan, and when to run it.
 *
 * A PLC works in scans. Each scan:
 *   1. reads every pin at once, with one read of GPIO_IN (the "input image");
 *   2. works through the program, keeping its results in RAM only;
 *   3. sets every output at once, with one write to GPIO_OUT_W1TS and one to
 *      GPIO_OUT_W1TC.
 * So a rung never sees a pin change halfway through a scan, and the outputs all
 * change together at the end.
 *
 * The program is a little stack machine. 'rlo' (the "result of logic
 * operation", the PLC name for it) is the power flowing along the rung so far.
 * A contact ANDs or ORs its value into it, and a coil copies it to an output.
 * LD in the middle of a rung starts a new block: the rung so far is pushed onto
 * a stack, and ANB or ORB later pops it and joins the two.
 *
 * Solution to exercise 12: the scans come from the 1 ms timer interrupt of
 * exercise 4. tick_handler() calls plc_tick() every millisecond, and every
 * 'period' ticks it runs a scan, whatever Lua is doing. plc_poll(), which the
 * console still calls, has nothing left to do.
 *
 * The interrupt and the rest of the program now share plc_image, so anything
 * outside the interrupt that changes it (loading, starting, stopping, a step,
 * plc.set()) turns interrupts off while it does: otherwise a scan could run
 * halfway through, and see half a program or undo a change. The interrupt
 * itself must not call uptime_cycles(), whose 64-bit total the main program
 * updates too, so scans time themselves with the 32-bit cycle counter.
 *
 * The C3U Viewer's commands (run, stop, set an M bit or a D register), which it
 * writes into plc_image over JTAG, are still carried out by plc_poll(),
 * outside the interrupt: plc_run() and plc_stop() turn interrupts off themselves.
 *
 * Read first: app/plc.h, then app/plc_asm.c.
 * Try this:   plc.load("LD M0\nOUT G4") plc.run() and then plc.set("M0", 1).
 *             Lua flips a relay, and the ladder does the rest.
 */
#include <string.h>
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"
#include "gpio.h"
#include "plc.h"

/* At PLC_IMAGE_ADDRESS, set in boot/c3u-metal.ld, where the viewer finds it. */
struct plc_image plc_image __attribute__((section(".plc_image")));

static uint32_t ticks_to_scan;          /* timer interrupts until the next scan */
static uint32_t last_scan;              /* cycle_count() at the last scan: timers add the time since */
static enum gpio_pull pulls[PLC_PIN_COUNT];     /* the pull resistor each input gets */

void plc_init(void)
{
    memset(&plc_image, 0, sizeof plc_image);    /* its section isn't cleared at start-up like .bss */
    plc_image.magic = PLC_MAGIC;
    plc_image.layout = PLC_LAYOUT;
    /* Inputs pull down, so a switch wired to 3.3 V reads 1 while it's pressed.
       The button's pin keeps the pull-up it needs: its switch goes to ground. */
    for (int pin = 0; pin < PLC_PIN_COUNT; pin++)
        pulls[pin] = (pin == BTN_PIN) ? GPIO_PULL_UP : GPIO_PULL_DOWN;
}

static inline bool get_bit(const uint32_t *bits, unsigned n)
{
    return (bits[n / 32] >> (n % 32)) & 1u;
}

static inline void put_bit(uint32_t *bits, unsigned n, bool value)
{
    if (value)
        bits[n / 32] |= 1u << (n % 32);
    else
        bits[n / 32] &= ~(1u << (n % 32));
}


/* ---- the pins ------------------------------------------------------------------- */

/* Set up the program's pins: coils as outputs (starting at 0), contacts as inputs. */
static void claim_pins(void)
{
    struct plc_image *p = &plc_image;

    for (int pin = 0; pin < PLC_PIN_COUNT; pin++) {
        if (p->out_mask & (1u << pin))
            gpio_output(pin);
        else if (p->in_mask & (1u << pin))
            gpio_input(pin, pulls[pin]);
    }
    p->outputs = 0;
    p->claimed = 1;
    last_scan = cycle_count();          /* timers start from now, not from the last time the ladder ran */
}

/* Turn the program's outputs off and give the pins back to Lua. */
static void release_pins(void)
{
    struct plc_image *p = &plc_image;

    if (p->claimed)
        REG(GPIO_OUT_W1TC) = p->out_mask;
    p->outputs = 0;
    p->claimed = 0;
}

void plc_set_pull(int pin, enum gpio_pull pull)
{
    cpu_interrupts_off();
    pulls[pin] = pull;
    if (plc_image.claimed && (plc_image.in_mask >> pin & 1u))
        gpio_input(pin, pull);          /* in use now: change it straight away */
    cpu_interrupts_on();
}

bool plc_owns_pin(int pin)
{
    const struct plc_image *p = &plc_image;

    return p->claimed && pin >= 0 && pin < PLC_PIN_COUNT && ((p->in_mask | p->out_mask) >> pin & 1u);
}


/* ---- the scan ------------------------------------------------------------------- */

/* The value of an instruction's operand, as the program sees it during this scan. */
static bool operand(uint32_t word)
{
    const struct plc_image *p = &plc_image;
    unsigned n = PLC_INDEX(word);

    switch (PLC_KIND(word)) {
    case KIND_G:
        /* An output pin reads back what the coils have set (the seal-in contact
           relies on this); an input pin reads the input image. */
        return ((p->out_mask >> n & 1u) ? p->outputs : p->inputs) >> n & 1u;
    case KIND_M:
        return get_bit(p->m, n);
    case KIND_T:
        return p->t[n].done;
    case KIND_C:
        return p->c[n].done;
    }
    return false;
}

/* A number from a value word (see PLC_VALUE in plc.h): a D register, a timer's ms, a count, or a constant. */
static int32_t read_number(uint32_t v)
{
    const struct plc_image *p = &plc_image;
    uint32_t n = PLC_VALUE_INDEX(v);

    switch (PLC_VALUE_KIND(v)) {
    case KIND_D: return p->d[n];
    case KIND_T: return (int32_t)(p->t[n].elapsed_us / 1000u);
    case KIND_C: return (int32_t)p->c[n].count;
    default:     return PLC_VALUE_NUMBER(v);
    }
}

/* A compare contact: LD>= D0 100 and the rest. */
static bool compare(unsigned cmp, int32_t a, int32_t b)
{
    switch (cmp) {
    case CMP_EQ: return a == b;
    case CMP_NE: return a != b;
    case CMP_GT: return a > b;
    case CMP_GE: return a >= b;
    case CMP_LT: return a < b;
    default:     return a <= b;
    }
}

/* MOV, ADD, SUB, MUL, DIV: work out a number into a D register. Sums wrap
   around at 32 bits, as they do in C on unsigned numbers. DIV by 0 leaves
   the result as it was. */
static void calculate(unsigned op, const uint32_t *v)
{
    struct plc_image *p = &plc_image;
    int32_t a = read_number(v[0]);
    int32_t *result = &p->d[PLC_VALUE_INDEX(v[op == OP_MOV ? 1 : 2])];
    int32_t b = op == OP_MOV ? 0 : read_number(v[1]);

    switch (op) {
    case OP_MOV: *result = a; break;
    case OP_ADD: *result = (int32_t)((uint32_t)a + (uint32_t)b); break;
    case OP_SUB: *result = (int32_t)((uint32_t)a - (uint32_t)b); break;
    case OP_MUL: *result = (int32_t)((uint32_t)a * (uint32_t)b); break;
    case OP_DIV:
        if (b == -1)
            *result = (int32_t)(0u - (uint32_t)a);  /* -2147483648 / -1 doesn't fit: wrap it like the rest */
        else if (b != 0)
            *result = a / b;
        break;
    }
}

/* A contact: does it let power through? Edge contacts remember their operand in p->edge. */
static bool contact(unsigned op, uint32_t word, unsigned i)
{
    struct plc_image *p = &plc_image;

    if (plc_op_is_compare(op))
        return compare(PLC_INDEX(word), read_number(p->program[i + 1]), read_number(p->program[i + 2]));

    bool now = operand(word);
    bool before;

    switch (op) {
    case OP_LDN: case OP_ANDN: case OP_ORN:
        return !now;
    case OP_LDP: case OP_ANDP: case OP_ORP:
        before = get_bit(p->edge, i);
        put_bit(p->edge, i, now);
        return now && !before;                  /* it has just turned on */
    case OP_LDF: case OP_ANDF: case OP_ORF:
        before = get_bit(p->edge, i);
        put_bit(p->edge, i, now);
        return !now && before;                  /* it has just turned off */
    default:
        return now;
    }
}

static void coil(unsigned op, uint32_t word, bool rlo)
{
    struct plc_image *p = &plc_image;
    unsigned n = PLC_INDEX(word);

    if (op == OP_OUT) {                         /* follows the rung */
    } else if (!rlo) {
        return;                                 /* SET and RST only act while the rung is on */
    } else if (op == OP_SET) {
        rlo = true;
    } else {                                    /* RST */
        rlo = false;
        if (PLC_KIND(word) == KIND_T) {
            p->t[n].elapsed_us = 0;
            p->t[n].done = 0;
        } else if (PLC_KIND(word) == KIND_C) {
            bool down = p->down_only >> n & 1u;   /* a CTD alone starts again from its preset */
            p->c[n].count = down ? p->c[n].preset : 0;
            p->c[n].done = 0;
        }
    }
    if (PLC_KIND(word) == KIND_G)
        put_bit(&p->outputs, n, rlo);
    else if (PLC_KIND(word) == KIND_M)
        put_bit(p->m, n, rlo);
}

/* A timer or counter, given the rung so far. Returns its output. */
static bool box(unsigned op, uint32_t word, uint32_t preset, bool rlo, uint32_t dt_us)
{
    struct plc_image *p = &plc_image;
    unsigned n = PLC_INDEX(word);

    if (op == OP_CTU || op == OP_CTD) {
        /* A CTU and a CTD may share a counter, so each keeps its own bit in 'last'. */
        struct plc_counter *c = &p->c[n];
        uint32_t bit = op == OP_CTU ? 1u : 2u;
        bool rising = rlo && !(c->last & bit);  /* the rung has just turned on */
        c->preset = preset;
        if (rising && op == OP_CTU && c->count < PLC_PRESET_MAX)
            c->count++;
        if (rising && op == OP_CTD && c->count > 0)
            c->count--;
        c->last = rlo ? (c->last | bit) : (c->last & ~bit);
        c->done = (p->down_only >> n & 1u) ? c->count == 0 : c->count >= c->preset;
        return c->done;
    }

    /* A timer adds the time since the last scan, but only if the rung was
       already on (TON, RTO) or off (TOF) then too, so it starts from 0. */
    struct plc_timer *t = &p->t[n];
    uint32_t preset_us = preset * 1000u;
    uint32_t add = (dt_us < preset_us - t->elapsed_us) ? dt_us : preset_us - t->elapsed_us;
    t->preset_ms = preset;
    if (op == OP_TON) {                         /* times while the rung is on */
        if (!rlo)
            t->elapsed_us = 0;
        else if (t->last)
            t->elapsed_us += add;
        t->done = rlo && t->elapsed_us >= preset_us;
    } else if (op == OP_RTO) {                  /* like TON, but keeps its time until RST */
        if (rlo && t->last)
            t->elapsed_us += add;
        t->done = t->elapsed_us >= preset_us;
    } else if (op == OP_TP) {                   /* a pulse of exactly the preset, whatever the rung does */
        if (t->done) {
            t->elapsed_us += add;
            if (t->elapsed_us >= preset_us)
                t->done = 0;                    /* over: elapsed stays at the preset until the rung goes off */
        } else if (rlo && !t->last) {
            t->elapsed_us = 0;                  /* the rung has just turned on: start a pulse */
            t->done = 1;
        } else if (!rlo) {
            t->elapsed_us = 0;
        }
    } else {                                    /* TOF: times once the rung goes off */
        if (rlo) {
            t->elapsed_us = 0;
            t->done = 1;
        } else if (t->done && !t->last) {
            t->elapsed_us += add;
            if (t->elapsed_us >= preset_us)
                t->done = 0;
        }
    }
    t->last = rlo;
    return t->done;
}

static void scan(void)
{
    struct plc_image *p = &plc_image;
    uint32_t start = cycle_count();
    uint32_t dt_us = (start - last_scan) / CPU_CYCLES_PER_US;   /* for the timers; right across a wrap */
    bool rlo = false;                   /* power along the rung so far */
    bool stack[PLC_STACK];              /* earlier blocks of this rung, waiting for ANB or ORB */
    unsigned sp = 0;
    bool rung_done = true;              /* true after a coil or a box: the next LD starts a new rung */

    /* A scan more than a millisecond later than its period is late: interrupts
       were held off for that long. (A step, from plc.step(), isn't counted.) */
    if (p->running && p->scans > 0 && dt_us > p->period_us + 1000)
        p->late++;
    last_scan = start;
    p->inputs = REG(GPIO_IN);                                   /* 1. the input image */

    for (unsigned i = 0; i < p->words; i += plc_op_words(PLC_OP(p->program[i]))) {
        uint32_t word = p->program[i];
        unsigned op = PLC_OP(word);
        bool value = false;

        switch (op) {                                           /* 2. the program */
        case OP_LD: case OP_LDN: case OP_LDP: case OP_LDF: case OP_LDC:
            value = contact(op, word, i);
            if (rung_done)
                sp = 0;
            else if (sp < PLC_STACK)
                stack[sp++] = rlo;
            rlo = value;
            rung_done = false;
            break;
        case OP_AND: case OP_ANDN: case OP_ANDP: case OP_ANDF: case OP_ANDC:
            value = contact(op, word, i);
            rlo = rlo && value;
            break;
        case OP_OR: case OP_ORN: case OP_ORP: case OP_ORF: case OP_ORC:
            value = contact(op, word, i);
            rlo = rlo || value;
            break;
        case OP_ANB:
            rlo = (sp > 0 && stack[--sp]) && rlo;
            value = rlo;
            break;
        case OP_ORB:
            rlo = (sp > 0 && stack[--sp]) || rlo;
            value = rlo;
            break;
        case OP_OUT: case OP_SET: case OP_RST:
            coil(op, word, rlo);
            value = rlo;
            rung_done = true;
            break;
        case OP_TON: case OP_TOF: case OP_TP: case OP_RTO: case OP_CTU: case OP_CTD:
            rlo = box(op, word, p->program[i + 1], rlo, dt_us);
            value = rlo;
            rung_done = true;
            break;
        case OP_MOV: case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            if (rlo)                                            /* like a coil, but it works out a number */
                calculate(op, &p->program[i + 1]);
            value = rlo;
            rung_done = true;
            break;
        }
        /* For a contact: does it let power through? For the rest: the result. */
        put_bit(p->trace, i, value);
    }

    if (p->claimed) {                                           /* 3. the outputs */
        REG(GPIO_OUT_W1TS) = p->outputs & p->out_mask;
        REG(GPIO_OUT_W1TC) = ~p->outputs & p->out_mask;
    }

    p->scans++;
    p->scan_us = (cycle_count() - start) / CPU_CYCLES_PER_US;
    if (p->scan_us > p->scan_us_max)
        p->scan_us_max = p->scan_us;
}


/* ---- loading, starting and stopping ------------------------------------------- */

void plc_install(const struct plc_program *prog, const char *source, size_t source_len)
{
    struct plc_image *p = &plc_image;
    bool was_claimed = p->claimed;

    cpu_interrupts_off();               /* no scan of half a program */
    release_pins();                     /* the old program's outputs go off */
    memcpy(p->program, prog->words, prog->count * sizeof prog->words[0]);
    p->words = prog->count;
    p->in_mask = prog->in_mask;
    p->out_mask = prog->out_mask;
    memcpy(p->source, source, source_len);
    p->source[source_len] = '\0';
    p->source_len = source_len;

    memset(p->m, 0, sizeof p->m);
    memset(p->t, 0, sizeof p->t);
    memset(p->c, 0, sizeof p->c);
    memset(p->trace, 0, sizeof p->trace);
    memset(p->edge, 0, sizeof p->edge);
    memset(p->d, 0, sizeof p->d);
    p->down_only = prog->down_only;
    for (unsigned i = 0; i < p->words; i += plc_op_words(PLC_OP(p->program[i]))) {
        uint32_t word = p->program[i];
        if (PLC_OP(word) == OP_CTD && (p->down_only >> PLC_INDEX(word) & 1u)) {
            p->c[PLC_INDEX(word)].preset = p->program[i + 1];  /* counting down starts from the preset */
            p->c[PLC_INDEX(word)].count = p->program[i + 1];
        }
    }
    p->scans = p->scan_us = p->scan_us_max = p->late = 0;
    p->loads++;
    if (was_claimed)
        claim_pins();                   /* a running ladder carries on with the new program */
    cpu_interrupts_on();
}

void plc_run(uint32_t period_us)
{
    struct plc_image *p = &plc_image;

    cpu_interrupts_off();
    if (!p->claimed)
        claim_pins();
    p->period_us = period_us;
    ticks_to_scan = 1;                  /* the first scan happens at the next tick */
    p->running = 1;
    cpu_interrupts_on();
}

void plc_stop(void)
{
    cpu_interrupts_off();
    plc_image.running = 0;
    release_pins();
    cpu_interrupts_on();
}

void plc_step(void)
{
    cpu_interrupts_off();               /* not at the same time as a scan from the interrupt */
    if (!plc_image.claimed)
        claim_pins();
    scan();
    cpu_interrupts_on();
}

/* A command the viewer wrote over JTAG (see PLC_CMD_RUN in plc.h). Carried out
   outside the interrupt, like plc.set(): an M bit is set with interrupts off,
   so a scan can't write the same word halfway through. */
static void obey(void)
{
    struct plc_image *p = &plc_image;
    uint32_t command = p->command;
    uint32_t arg = PLC_CMD_ARG(command);

    p->command = 0;                     /* taken: the viewer may write the next one */
    p->commands_done++;
    switch (PLC_CMD(command)) {
    case PLC_CMD_RUN:
        if (p->words > 0 && arg >= 1 && arg <= 1000)
            plc_run(arg * 1000u);
        break;
    case PLC_CMD_STOP:
        plc_stop();
        break;
    case PLC_CMD_SET_M:
        if ((arg & 0xFFu) < PLC_M_COUNT) {
            cpu_interrupts_off();
            put_bit(p->m, arg & 0xFFu, arg & 0x100u);
            cpu_interrupts_on();
        }
        break;
    case PLC_CMD_SET_D:
        if (arg < PLC_D_COUNT)
            p->d[arg] = (int32_t)p->command_value;
        break;
    }
}

/* Called by tick_handler() (drivers/tick.c) every millisecond, with interrupts off. */
void plc_tick(void)
{
    if (!plc_image.running || --ticks_to_scan > 0)
        return;
    ticks_to_scan = plc_image.period_us / 1000;
    scan();
}

/* The interrupt runs the scans now. All that is left here is the viewer's commands. */
void plc_poll(void)
{
    if (plc_image.command != 0)
        obey();
}
