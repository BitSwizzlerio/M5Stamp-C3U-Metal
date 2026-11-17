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
 * There are no interrupts in C3U-Metal, so nothing interrupts Lua to run a
 * scan. Instead plc_poll() is called from the places that already check
 * something regularly: the prompt while it waits for a key, and the Ctrl-C
 * check, which runs every 1000 Lua instructions and all through delay(). Each
 * call is cheap unless a scan is due.
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
#include "uptime.h"

struct plc_image plc_image;             /* in .bss: plc_init() fills in the rest */

static uint64_t next_scan;              /* uptime_cycles() at which the next scan is due */
static uint64_t last_scan;              /* uptime_cycles() at the last scan: timers add the time since */
static enum gpio_pull pulls[PLC_PIN_COUNT];     /* the pull resistor each input gets */

void plc_init(void)
{
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
    last_scan = uptime_cycles();        /* timers start from now, not from the last time the ladder ran */
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

/* A contact: does it let power through? Edge contacts remember their operand in p->edge. */
static bool contact(unsigned op, uint32_t word, unsigned i)
{
    struct plc_image *p = &plc_image;
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
            p->c[n].count = 0;
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

    if (op == OP_CTU) {
        struct plc_counter *c = &p->c[n];
        c->preset = preset;
        if (rlo && !c->last && c->count < PLC_PRESET_MAX)
            c->count++;                         /* the rung has just turned on */
        c->last = rlo;
        c->done = c->count >= c->preset;
        return c->done;
    }

    /* A timer adds the time since the last scan, but only if the rung was
       already on (TON) or off (TOF) then too, so it starts from 0. */
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
    uint64_t now = uptime_cycles();
    uint32_t dt_us = (uint32_t)((now - last_scan) / CPU_CYCLES_PER_US);   /* for the timers */
    bool rlo = false;                   /* power along the rung so far */
    bool stack[PLC_STACK];              /* earlier blocks of this rung, waiting for ANB or ORB */
    unsigned sp = 0;
    bool rung_done = true;              /* true after a coil or a box: the next LD starts a new rung */

    last_scan = now;
    p->inputs = REG(GPIO_IN);                                   /* 1. the input image */

    for (unsigned i = 0; i < p->words; i += plc_op_words(PLC_OP(p->program[i]))) {
        uint32_t word = p->program[i];
        unsigned op = PLC_OP(word);
        bool value = false;

        switch (op) {                                           /* 2. the program */
        case OP_LD: case OP_LDN: case OP_LDP: case OP_LDF:
            value = contact(op, word, i);
            if (rung_done)
                sp = 0;
            else if (sp < PLC_STACK)
                stack[sp++] = rlo;
            rlo = value;
            rung_done = false;
            break;
        case OP_AND: case OP_ANDN: case OP_ANDP: case OP_ANDF:
            value = contact(op, word, i);
            rlo = rlo && value;
            break;
        case OP_OR: case OP_ORN: case OP_ORP: case OP_ORF:
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
        case OP_TON: case OP_TOF: case OP_CTU:
            rlo = box(op, word, p->program[i + 1], rlo, dt_us);
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
    p->scans = p->scan_us = p->scan_us_max = p->late = 0;
    p->loads++;
    if (was_claimed)
        claim_pins();                   /* a running ladder carries on with the new program */
}

void plc_run(uint32_t period_us)
{
    struct plc_image *p = &plc_image;

    if (!p->claimed)
        claim_pins();
    p->period_us = period_us;
    p->running = 1;
    next_scan = uptime_cycles();        /* the first scan happens at the next poll */
}

void plc_stop(void)
{
    plc_image.running = 0;
    release_pins();
}

void plc_step(void)
{
    if (!plc_image.claimed)
        claim_pins();
    scan();
}

void plc_poll(void)
{
    struct plc_image *p = &plc_image;

    if (!p->running)
        return;
    uint64_t now = uptime_cycles();
    if (now < next_scan)
        return;
    scan();
    next_scan += (uint64_t)p->period_us * CPU_CYCLES_PER_US;
    if (next_scan <= now) {             /* a whole period was missed: don't try to catch up */
        p->late++;
        next_scan = now + (uint64_t)p->period_us * CPU_CYCLES_PER_US;
    }
}
