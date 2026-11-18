/*
 * plc_asm.c - the ladder's assembler: IL text in, instruction words out.
 *
 * IL (Instruction List) is a PLC's assembly language. Each line holds one
 * instruction and its operand, and a rung is read from left to right:
 *
 *     LD   G8      ; start          a contact: power passes while G8 is 1
 *     OR   G4      ; seal-in        ... or while G4 is 1, in parallel with it
 *     ANDN G10     ; stop           ... and while G10 is 0, in series
 *     OUT  G4      ; motor          the coil: G4 follows the result
 *     LD   G4
 *     TON  T0 2000                  a timer: on once G4 has been on for 2000 ms
 *     OUT  G5      ; fan
 *
 * A rung is: contacts, then optionally timers or counters, then coils.
 * Operands are the board's own pin names, G0 to G21, plus M0 to M255 (relays
 * inside the PLC), T0 to T31 (timers) and C0 to C31 (counters). A pin with a
 * coil is an output; a pin only read by contacts is an input. Everything after
 * ';' (or "--" or "//") is a comment.
 *
 * The assembler checks everything it can, so the scan in plc.c can trust the
 * program and never has to check anything while it runs.
 *
 * Read first: app/plc.h.
 * Try this:   plc.load("LD G8\nAND G10") and read the message. Then add the
 *             missing line and load it again.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "gpio.h"
#include "plc.h"

enum role {
    LOAD,                               /* starts a rung or a block */
    JOIN,                               /* a contact in series or parallel */
    BLOCK,                              /* ANB, ORB */
    BOX,                                /* a timer or counter: takes the rung and gives its own output */
    COIL,                               /* ends the rung */
};

#define G (1u << KIND_G)
#define M (1u << KIND_M)
#define T (1u << KIND_T)
#define C (1u << KIND_C)

static const struct {
    const char *name;
    enum role role;
    unsigned kinds;                     /* operands it takes */
} ops[OP_COUNT] = {
    [OP_LD]   = { "LD",   LOAD, G|M|T|C }, [OP_LDN]  = { "LDN",  LOAD, G|M|T|C },
    [OP_LDP]  = { "LDP",  LOAD, G|M|T|C }, [OP_LDF]  = { "LDF",  LOAD, G|M|T|C },
    [OP_AND]  = { "AND",  JOIN, G|M|T|C }, [OP_ANDN] = { "ANDN", JOIN, G|M|T|C },
    [OP_ANDP] = { "ANDP", JOIN, G|M|T|C }, [OP_ANDF] = { "ANDF", JOIN, G|M|T|C },
    [OP_OR]   = { "OR",   JOIN, G|M|T|C }, [OP_ORN]  = { "ORN",  JOIN, G|M|T|C },
    [OP_ORP]  = { "ORP",  JOIN, G|M|T|C }, [OP_ORF]  = { "ORF",  JOIN, G|M|T|C },
    [OP_ANB]  = { "ANB",  BLOCK, 0 },      [OP_ORB]  = { "ORB",  BLOCK, 0 },
    [OP_TON]  = { "TON",  BOX, T },        [OP_TOF]  = { "TOF",  BOX, T },
    [OP_TP]   = { "TP",   BOX, T },        [OP_RTO]  = { "RTO",  BOX, T },
    [OP_CTU]  = { "CTU",  BOX, C },        [OP_CTD]  = { "CTD",  BOX, C },
    [OP_OUT]  = { "OUT",  COIL, G|M },     [OP_SET]  = { "SET",  COIL, G|M },
    [OP_RST]  = { "RST",  COIL, G|M|T|C },
};
#undef G
#undef M
#undef T
#undef C

const char *plc_op_name(unsigned op)    { return op < OP_COUNT ? ops[op].name : "?"; }
unsigned plc_op_words(unsigned op)      { return op < OP_COUNT && ops[op].role == BOX ? 2 : 1; }
bool plc_op_is_contact(unsigned op)     { return op < OP_COUNT && (ops[op].role == LOAD || ops[op].role == JOIN); }
bool plc_op_is_output(unsigned op)      { return op < OP_COUNT && ops[op].role == COIL; }
bool plc_op_is_box(unsigned op)         { return op < OP_COUNT && ops[op].role == BOX; }

static const char kind_letter[] = { [KIND_G] = 'G', [KIND_M] = 'M', [KIND_T] = 'T', [KIND_C] = 'C' };
static const unsigned kind_count[] = {
    [KIND_G] = PLC_PIN_COUNT, [KIND_M] = PLC_M_COUNT, [KIND_T] = PLC_T_COUNT, [KIND_C] = PLC_C_COUNT,
};

void plc_operand_name(uint32_t word, char *buf, size_t size)
{
    snprintf(buf, size, "%c%u", kind_letter[PLC_KIND(word)], (unsigned)PLC_INDEX(word));
}

bool plc_parse_operand(const char *name, size_t len, unsigned *kind, unsigned *index)
{
    if (len < 2 || len > 4)
        return false;
    char letter = (char)toupper((unsigned char)name[0]);
    for (unsigned k = 0; k < sizeof kind_letter; k++) {
        if (letter != kind_letter[k])
            continue;
        unsigned n = 0;
        for (size_t i = 1; i < len; i++) {
            if (!isdigit((unsigned char)name[i]))
                return false;
            n = n * 10 + (unsigned)(name[i] - '0');
        }
        if (len > 2 && name[1] == '0')
            return false;                       /* "G04" is a typing mistake, not G4 */
        if (n >= kind_count[k])
            return false;
        *kind = k;
        *index = n;
        return true;
    }
    return false;
}


/* ---- reading the text --------------------------------------------------------- */

struct assembler {
    struct plc_program *prog;
    char *error;
    size_t error_size;
    unsigned line;
    unsigned coil_line[2][PLC_M_COUNT]; /* where each G and M got its OUT, to catch a second one */
    unsigned timer_line[PLC_T_COUNT];   /* where each timer got its TON, TOF, TP or RTO */
    unsigned up_line[PLC_C_COUNT];      /* where each counter got its CTU ... */
    unsigned down_line[PLC_C_COUNT];    /* ... and its CTD: one of each may share a counter */
    uint32_t counter_preset[PLC_C_COUNT];
};

static bool fail(struct assembler *a, const char *format, ...)
{
    va_list args;
    int n = snprintf(a->error, a->error_size, "line %u: ", a->line);

    va_start(args, format);
    if (n > 0 && (size_t)n < a->error_size)
        vsnprintf(a->error + n, a->error_size - (size_t)n, format, args);
    va_end(args);
    return false;
}

/* The next word on the line, without a '\r' at its end. Returns its length, 0 at the end of the line. */
static size_t next_word(const char **p, const char *end, const char **word)
{
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r'))
        (*p)++;
    *word = *p;
    while (*p < end && **p != ' ' && **p != '\t' && **p != '\r')
        (*p)++;
    return (size_t)(*p - *word);
}

/* Where the comment starts on a line, or 'end' if there isn't one. */
static const char *comment_start(const char *p, const char *end)
{
    for (; p < end; p++) {
        if (*p == ';')
            return p;
        if (p + 1 < end && ((p[0] == '-' && p[1] == '-') || (p[0] == '/' && p[1] == '/')))
            return p;
    }
    return end;
}

/* What to say when an instruction is given the wrong kind of operand. */
static bool wrong_kind(struct assembler *a, unsigned op, unsigned kind, unsigned index)
{
    char letter = kind_letter[kind];

    if (ops[op].role == BOX && ops[op].kinds == (1u << KIND_T))
        return fail(a, "%s needs a timer, T0 to T31, then a time in ms: %s T0 500", ops[op].name, ops[op].name);
    if (ops[op].role == BOX)
        return fail(a, "%s needs a counter, C0 to C31, then a count: %s C0 10", ops[op].name, ops[op].name);
    return fail(a, "%s can't drive %c%u: timers and counters are set by their own boxes "
                   "(TON, TOF, TP, RTO, CTU, CTD), and only reset with RST", ops[op].name, letter, index);
}

/* Check that an operand may be used here, and note which pins are inputs and outputs. */
static bool check_operand(struct assembler *a, unsigned op, unsigned kind, unsigned index)
{
    bool coil = ops[op].role == COIL;

    if (!(ops[op].kinds & (1u << kind)))
        return wrong_kind(a, op, kind, index);
    if (kind == KIND_G) {
        const char *problem = gpio_check_pin((int)index);
        if (problem != NULL)
            return fail(a, "G%u can't be used: %s", index, problem);
        if (index == LED_PIN)
            return fail(a, "G%u drives the RGB LED: use led() from Lua instead", index);
        if (coil && index == BTN_PIN)
            return fail(a, "G%u is the button: the ladder can read it (LDN G%u is true while it's held) "
                           "but not drive it", index, index);
        if (coil)
            a->prog->out_mask |= 1u << index;
        else
            a->prog->in_mask |= 1u << index;
    }
    if (op == OP_OUT) {
        unsigned *first = &a->coil_line[kind][index];
        if (*first != 0)
            return fail(a, "%c%u already has a coil, on line %u. Give each output one OUT, "
                           "and combine conditions in the rung (or with M bits)",
                        kind_letter[kind], index, *first);
        *first = a->line;
    }
    if (ops[op].role == BOX) {
        unsigned *first = kind == KIND_T ? &a->timer_line[index]
                        : op == OP_CTU ? &a->up_line[index] : &a->down_line[index];
        if (*first != 0)
            return fail(a, "%c%u is already used, on line %u: give each timer or counter its own number%s",
                        kind_letter[kind], index, *first,
                        kind == KIND_C ? " (one CTU and one CTD may share a counter)" : "");
        *first = a->line;
    }
    return true;
}

/* A CTU and a CTD that share a counter must agree on its preset. */
static bool check_counter_preset(struct assembler *a, unsigned op, unsigned index, uint32_t preset)
{
    uint32_t *known = &a->counter_preset[index];
    unsigned other = op == OP_CTU ? a->down_line[index] : a->up_line[index];

    if (*known != 0 && *known != preset)
        return fail(a, "C%u's %s says %u, but its %s on line %u says %u: give them the same count",
                    index, ops[op].name, (unsigned)preset, op == OP_CTU ? "CTD" : "CTU", other, (unsigned)*known);
    *known = preset;
    return true;
}

/* Read a timer's time or a counter's count. */
static bool read_preset(struct assembler *a, unsigned op, const char *word, size_t len, uint32_t *preset)
{
    uint32_t n = 0;
    bool counter = ops[op].kinds == (1u << KIND_C);
    const char *unit = counter ? "count" : "time in ms";

    if (len == 0)
        return fail(a, "%s needs a %s after the %s: %s %s %s",
                    ops[op].name, unit, counter ? "counter" : "timer", ops[op].name,
                    counter ? "C0" : "T0", counter ? "10" : "500");
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)word[i]))
            return fail(a, "'%.*s' isn't a %s: write a whole number, such as 500", (int)len, word, unit);
        if (n > PLC_PRESET_MAX)
            break;
        n = n * 10 + (uint32_t)(word[i] - '0');
    }
    if (n < 1 || n > PLC_PRESET_MAX)
        return fail(a, "the %s must be 1 to %u", unit, PLC_PRESET_MAX);
    *preset = n;
    return true;
}

bool plc_assemble(const char *text, size_t len, struct plc_program *prog, char *error, size_t error_size)
{
    static struct assembler a;              /* static: the tables are 2 KB, too much for the stack */
    const char *p = text, *end = text + len;
    unsigned depth = 0;                     /* values on the stack: 1 inside a rung, more inside blocks */
    bool boxed = false;                     /* the rung has reached its timers or counters */
    bool outputs = false;                   /* the rung has reached its coils */

    memset(&a, 0, sizeof a);
    memset(prog, 0, sizeof *prog);
    a.prog = prog;
    a.error = error;
    a.error_size = error_size;

    if (len >= PLC_SOURCE_MAX) {
        snprintf(error, error_size, "the program is %u characters long; the most is %u, comments included",
                 (unsigned)len, PLC_SOURCE_MAX - 1);
        return false;
    }

    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (line_end == NULL)
            line_end = end;
        a.line++;

        const char *code_end = comment_start(p, line_end);
        const char *word;
        size_t n = next_word(&p, code_end, &word);
        if (n == 0) {                       /* blank, or only a comment */
            p = line_end < end ? line_end + 1 : end;
            continue;
        }

        /* The instruction */
        char name[8];
        unsigned op;
        if (n >= sizeof name)
            return fail(&a, "unknown instruction '%.*s'", (int)n, word);
        for (size_t i = 0; i < n; i++)
            name[i] = (char)toupper((unsigned char)word[i]);
        name[n] = '\0';
        for (op = 0; op < OP_COUNT; op++)
            if (strcmp(name, ops[op].name) == 0)
                break;
        if (op == OP_COUNT)
            return fail(&a, "unknown instruction '%.*s'", (int)n, word);
        enum role role = ops[op].role;

        /* Its operand, and a box's preset */
        unsigned kind = 0, index = 0;
        uint32_t preset = 0;
        const char *arg;
        size_t arg_len = next_word(&p, code_end, &arg);
        if (role == BLOCK) {
            if (arg_len > 0)
                return fail(&a, "%s takes no operand: it joins the last two blocks", ops[op].name);
        } else {
            if (arg_len == 0)
                return fail(&a, "%s needs an operand, such as G8 or M0", ops[op].name);
            if (!plc_parse_operand(arg, arg_len, &kind, &index))
                return fail(&a, "'%.*s' isn't something the ladder knows: use G0 to G21 (the pins), "
                                "M0 to M255, T0 to T31 or C0 to C31", (int)arg_len, arg);
            if (!check_operand(&a, op, kind, index))
                return false;
        }
        if (role == BOX) {
            arg_len = next_word(&p, code_end, &arg);
            if (!read_preset(&a, op, arg, arg_len, &preset))
                return false;
            if (kind == KIND_C && !check_counter_preset(&a, op, index, preset))
                return false;
        }
        arg_len = next_word(&p, code_end, &arg);
        if (arg_len > 0)
            return fail(&a, "unexpected '%.*s' after %s (start a comment with ';')", (int)arg_len, arg, name);

        /* Does it fit the rung so far? */
        switch (role) {
        case LOAD:
            if (depth == 0 || boxed || outputs) {   /* a new rung */
                depth = 1;
                boxed = outputs = false;
                prog->rungs++;
            } else if (++depth > PLC_STACK) {
                return fail(&a, "more than %u blocks open at once", PLC_STACK);
            }
            break;
        case JOIN:
        case BLOCK:
            if (depth == 0)
                return fail(&a, "%s has nothing to join to: start the rung with LD", ops[op].name);
            if (outputs)
                return fail(&a, "%s after a coil: start a new rung with LD", ops[op].name);
            if (boxed)
                return fail(&a, "%s after a timer or counter: put the contacts before it, "
                                "or start a new rung with LD", ops[op].name);
            if (role == BLOCK && depth < 2)
                return fail(&a, "%s needs two blocks, and there is only one. Each block starts with LD",
                            ops[op].name);
            if (role == BLOCK)
                depth--;
            break;
        case BOX:
        case COIL:
            if (depth == 0)
                return fail(&a, "%s has nothing to switch it: start the rung with LD", ops[op].name);
            if (depth > 1)
                return fail(&a, "%u blocks are still open: join them with ANB or ORB first", depth);
            if (role == BOX && outputs)
                return fail(&a, "%s after a coil: start a new rung with LD", ops[op].name);
            if (role == BOX)
                boxed = true;
            else
                outputs = true;
            break;
        }

        if (prog->count + plc_op_words(op) > PLC_MAX_WORDS)
            return fail(&a, "the program is too long: it can have %u words", PLC_MAX_WORDS);
        prog->words[prog->count++] = PLC_WORD(op, kind, index, a.line);
        if (role == BOX)
            prog->words[prog->count++] = preset;
        prog->instructions++;
        p = line_end < end ? line_end + 1 : end;
    }

    if (depth > 0 && !outputs && !boxed)
        return fail(&a, "the last rung has no coil: end it with OUT");
    prog->in_mask &= ~prog->out_mask;       /* a pin with a coil is an output, even if contacts read it */
    for (unsigned n = 0; n < PLC_C_COUNT; n++)
        if (a.down_line[n] && !a.up_line[n])
            prog->down_only |= 1u << n;     /* counts down from its preset to 0 */
    return true;
}
