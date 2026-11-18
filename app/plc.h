/*
 * plc.h - a small PLC (programmable logic controller) that runs beside Lua.
 *
 * A ladder program is written in IL (Instruction List) and turned into words
 * by plc_asm.c. plc.c runs it again and again: one "scan" reads every pin,
 * works through the instructions and then sets every output at once.
 *
 * Everything the ladder knows is in one struct, plc_image, so that Lua, the
 * console and (over JTAG) the C3U Viewer can all see the same thing.
 */
#ifndef PLC_H
#define PLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gpio.h"

#define PLC_MAX_WORDS   512             /* program size, in 32-bit words */
#define PLC_SOURCE_MAX  4096            /* the IL text, comments included, kept for plc.list() */
#define PLC_M_COUNT     256             /* internal relays M0 to M255 */
#define PLC_PIN_COUNT   22              /* G0 to G21 */
#define PLC_STACK       8               /* blocks a rung can have open at once */

#define PLC_MAGIC       0x5244414Cu     /* "LADR" in memory: the viewer looks for it */
#define PLC_LAYOUT      3               /* changes whenever struct plc_image changes */
#define PLC_IMAGE_ADDRESS 0x3FCBE000u   /* where plc_image lives: boot/c3u-metal.ld puts it there */

/*
 * The instructions. Their numbers are stored in programs, so new ones only
 * ever go at the end.
 */
enum plc_op {
    OP_LD, OP_LDN,                      /* start a rung, or a block inside one, with a contact */
    OP_AND, OP_ANDN,                    /* a contact in series */
    OP_OR, OP_ORN,                      /* a contact in parallel */
    OP_ANB, OP_ORB,                     /* join the last two blocks in series / in parallel */
    OP_OUT,                             /* a coil: the operand follows the rung */
    OP_LDP, OP_LDF,                     /* edge contacts: true for one scan when the operand */
    OP_ANDP, OP_ANDF,                   /*   turns on (P, "positive") or off (F, "falling") */
    OP_ORP, OP_ORF,
    OP_SET, OP_RST,                     /* coils that latch: SET turns on, RST turns off (or resets T, C) */
    OP_TON,                             /* on-delay timer: on once the rung has been on for the preset */
    OP_TOF,                             /* off-delay timer: stays on for the preset after the rung goes off */
    OP_CTU,                             /* counter: counts the rung turning on, on once it reaches the preset */
    OP_TP,                              /* pulse timer: on for exactly the preset from the rung turning on */
    OP_RTO,                             /* retentive timer: like TON, but keeps its time while the rung is off */
    OP_CTD,                             /* counts down. Alone: starts at the preset, on at 0. With a CTU
                                           on the same counter: an up/down counter, on at the preset */
    OP_LDC, OP_ANDC, OP_ORC,            /* compare contacts, written LD>= D0 100, AND= T0 500, OR<> C1 D2:
                                           power passes while the comparison is true (enum plc_cmp) */
    OP_MOV,                             /* MOV a D: D = a, every scan while the rung is on */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,     /* ADD a b D: D = a + b, and so on, while the rung is on */
    OP_COUNT
};

/* A compare contact's comparison, kept where other instructions keep their operand's number. */
enum plc_cmp { CMP_EQ, CMP_NE, CMP_GT, CMP_GE, CMP_LT, CMP_LE, CMP_COUNT };

/* What an operand names. */
enum plc_kind {
    KIND_G,                             /* a pin, G0 to G21 */
    KIND_M,                             /* an internal relay, M0 to M255 */
    KIND_T,                             /* a timer, T0 to T31: as a contact, "done" */
    KIND_C,                             /* a counter, C0 to C31: as a contact, "done" */
    KIND_D,                             /* a data register, D0 to D63: a number, not a bit */
    KIND_K,                             /* a number written in the program, such as 100 or -5 */
};

#define PLC_T_COUNT     32
#define PLC_C_COUNT     32
#define PLC_D_COUNT     64
#define PLC_PRESET_MAX  3600000u        /* one hour in ms, and the most a counter counts to */

struct plc_timer {
    uint32_t elapsed_us;                /* how long it has been timing */
    uint32_t preset_ms;                 /* from the program */
    uint32_t done;                      /* 1 when the timer's output is on */
    uint32_t last;                      /* the rung in the last scan: timing starts from the scan that sees it change */
};

struct plc_counter {
    uint32_t count;
    uint32_t preset;                    /* from the program */
    uint32_t done;                      /* 1 once count reaches preset (a CTD alone: once it reaches 0) */
    uint32_t last;                      /* the rungs in the last scan, to see them turn on:
                                           bit 0 the CTU's, bit 1 the CTD's */
};

/*
 * One instruction word:
 *
 *   bits 31-16  the line of the IL text it came from (for plc.list() and errors)
 *   bits 15-8   the operand's number, e.g. 4 for G4
 *   bits  7-6   the operand's kind (enum plc_kind)
 *   bits  5-0   the instruction (enum plc_op)
 *
 * Timers and counters (TON, TOF, TP, RTO, CTU, CTD) have a second word: the
 * preset, in ms or counts.
 *
 * Instructions on numbers (compare contacts, MOV, ADD, ...) are followed by one
 * word for each value they use, because a number needs more room:
 *
 *   bits 31-28  its kind: KIND_D, KIND_T (the time in ms), KIND_C (the count),
 *               or KIND_K, a number written in the program
 *   bits 27-0   the register's number, or for KIND_K the number itself
 *               (-134217728 to 134217727)
 */
#define PLC_WORD(op, kind, index, line) \
    ((uint32_t)(op) | (uint32_t)(kind) << 6 | (uint32_t)(index) << 8 | (uint32_t)(line) << 16)
#define PLC_OP(w)       ((w) & 0x3Fu)
#define PLC_KIND(w)     (((w) >> 6) & 0x3u)
#define PLC_INDEX(w)    (((w) >> 8) & 0xFFu)
#define PLC_LINE(w)     ((w) >> 16)

#define PLC_VALUE(kind, n)      ((uint32_t)(kind) << 28 | ((uint32_t)(n) & 0x0FFFFFFFu))
#define PLC_VALUE_KIND(w)       ((w) >> 28)
#define PLC_VALUE_INDEX(w)      ((w) & 0x0FFFFFFFu)
#define PLC_VALUE_NUMBER(w)     ((int32_t)((w) << 4) >> 4)      /* KIND_K: the 28-bit number, sign and all */
#define PLC_NUMBER_MIN          (-134217728)
#define PLC_NUMBER_MAX          134217727

/*
 * The ladder's state, at PLC_IMAGE_ADDRESS. The order of the fields is fixed by
 * PLC_LAYOUT, because the viewer reads them by their position over JTAG, while
 * the program runs: try hex(peek(0x3FCBE000)) to see the magic number.
 */
struct plc_image {
    uint32_t magic;                     /* PLC_MAGIC */
    uint32_t layout;                    /* PLC_LAYOUT */
    uint32_t loads;                     /* counts plc.load(): a new number means a new program */
    uint32_t running;                   /* 1 while scans happen by themselves */
    uint32_t claimed;                   /* 1 while the ladder has set up and owns its pins */
    uint32_t period_us;                 /* time from one scan to the next */
    uint32_t scans;                     /* scans since the program was loaded */
    uint32_t scan_us;                   /* how long the last scan took */
    uint32_t scan_us_max;               /* the longest scan so far */
    uint32_t late;                      /* scans that started a whole period late */
    uint32_t in_mask;                   /* pins read by contacts (bit n is Gn) */
    uint32_t out_mask;                  /* pins driven by coils */
    uint32_t inputs;                    /* GPIO_IN, read at the start of the last scan */
    uint32_t outputs;                   /* what the coils set, written to the pins at the end */
    uint32_t m[PLC_M_COUNT / 32];       /* M0 to M255, one bit each */
    struct plc_timer t[PLC_T_COUNT];
    struct plc_counter c[PLC_C_COUNT];
    uint32_t trace[PLC_MAX_WORDS / 32]; /* bit i: instruction word i passed power in the last scan */
    uint32_t edge[PLC_MAX_WORDS / 32];  /* bit i: an edge contact's operand in the last scan */
    uint32_t words;                     /* program length in words */
    uint32_t program[PLC_MAX_WORDS];
    uint32_t source_len;                /* the IL text, as it was loaded */
    char     source[PLC_SOURCE_MAX];
    /* Layout 2 added the fields from here on. */
    uint32_t command;                   /* written over JTAG by the viewer: PLC_CMD_*; 0 once carried out */
    uint32_t commands_done;             /* counts the commands carried out */
    uint32_t down_only;                 /* bit n: Cn has a CTD and no CTU, so it counts down to 0 */
    int32_t  d[PLC_D_COUNT];            /* D0 to D63: numbers, shared with Lua by plc.get() and plc.set() */
    /* Layout 3 added the field from here on. */
    uint32_t command_value;             /* PLC_CMD_SET_D's number: written over JTAG before the command */
};

/*
 * Commands from outside. The C3U Viewer writes one into plc_image.command over
 * JTAG, only while it reads 0, and plc_poll() carries it out between scans and
 * sets it back to 0. So the viewer can start and stop the ladder even while a
 * Lua program keeps the console busy. The low 8 bits are the command, the rest
 * its argument.
 */
#define PLC_CMD_RUN     1u              /* argument: the scan period in ms, 1 to 1000 */
#define PLC_CMD_STOP    2u
#define PLC_CMD_SET_M   3u              /* argument: the M bit's number, plus 0x100 to set it to 1 */
#define PLC_CMD_SET_D   4u              /* argument: the D register's number; its new value is in command_value */
#define PLC_CMD(w)      ((w) & 0xFFu)
#define PLC_CMD_ARG(w)  ((w) >> 8)

extern struct plc_image plc_image;

/* A program on its way from plc_asm.c to the engine. */
struct plc_program {
    uint32_t words[PLC_MAX_WORDS];
    unsigned count;                     /* words used */
    unsigned instructions;
    unsigned rungs;
    uint32_t in_mask, out_mask;
    uint32_t down_only;                 /* counters with a CTD and no CTU */
};

/* ---- plc_asm.c --------------------------------------------------------------- */

/*
 * Turn IL text into instruction words. Returns true on success; otherwise
 * 'error' says what is wrong and on which line.
 */
bool plc_assemble(const char *text, size_t len, struct plc_program *prog, char *error, size_t error_size);

/* Read an operand name such as "G4", "m10" or "D3". Returns false if it isn't one. */
bool plc_parse_operand(const char *name, size_t len, unsigned *kind, unsigned *index);

const char *plc_op_name(unsigned op);           /* "LD", "ANDN", ... */
unsigned plc_op_words(unsigned op);             /* how many words the instruction takes */
bool plc_op_is_contact(unsigned op);
bool plc_op_is_output(unsigned op);             /* OUT, SET, RST */
bool plc_op_is_box(unsigned op);                /* the timers and counters */
bool plc_op_is_compare(unsigned op);            /* LDC, ANDC, ORC */
bool plc_op_is_load(unsigned op);               /* LD, LDN, LDP, LDF, LDC: start a rung or a block */
bool plc_op_is_series(unsigned op);             /* AND, ANDN, ANDP, ANDF, ANDC */
void plc_operand_name(uint32_t word, char *buf, size_t size);   /* "G4", "M10", "T0" */
void plc_value_name(uint32_t value, char *buf, size_t size);    /* "D3", "T0", "-5" */
const char *plc_cmp_symbol(unsigned cmp);       /* ">=" for CMP_GE, ... */

/* The instruction at program[i] as IL, such as "LD>= D0 100" or "ADD D0 1 D0". */
void plc_instruction_text(const uint32_t *program, unsigned i, char *buf, size_t size);

/* ---- plc_show.c ---------------------------------------------------------------- */

/*
 * Print the program as ASCII rungs, with power from the last scan. Returns the
 * lines printed. 'workspace' is plc_show_workspace_size() bytes of memory to
 * draw in, so that drawing costs no RAM the rest of the time.
 */
unsigned plc_show(void *workspace, bool colour);
size_t plc_show_workspace_size(void);

/* ---- plc.c ----------------------------------------------------------------- */

void plc_init(void);                    /* once, at start-up */

/* Replace the program. A running ladder keeps running, now with the new program. */
void plc_install(const struct plc_program *prog, const char *source, size_t source_len);

void plc_run(uint32_t period_us);       /* start scanning by itself */
void plc_stop(void);                    /* stop scanning; outputs go to 0; the pins go back to Lua */
void plc_step(void);                    /* one scan now */
bool plc_owns_pin(int pin);             /* true if the ladder has set this pin up and uses it */
void plc_set_pull(int pin, enum gpio_pull pull);   /* for when the pin is a ladder input; default down */

/*
 * Run a scan if one is due, after carrying out a command from the viewer if
 * there is one. The places that already poll call this: the prompt's wait for
 * a key, the Ctrl-C check while Lua runs, and delay().
 */
void plc_poll(void);

#endif
