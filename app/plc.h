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
#define PLC_LAYOUT      1               /* changes whenever struct plc_image changes */

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
    OP_COUNT
};

/* What an operand names. */
enum plc_kind {
    KIND_G,                             /* a pin, G0 to G21 */
    KIND_M,                             /* an internal relay, M0 to M255 */
    KIND_T,                             /* a timer, T0 to T31: as a contact, "done" */
    KIND_C,                             /* a counter, C0 to C31: as a contact, "done" */
};

#define PLC_T_COUNT     32
#define PLC_C_COUNT     32
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
    uint32_t done;                      /* 1 once count reaches preset */
    uint32_t last;                      /* the rung in the last scan, to see it turn on */
};

/*
 * One instruction word:
 *
 *   bits 31-16  the line of the IL text it came from (for plc.list() and errors)
 *   bits 15-8   the operand's number, e.g. 4 for G4
 *   bits  7-6   the operand's kind (enum plc_kind)
 *   bits  5-0   the instruction (enum plc_op)
 *
 * TON, TOF and CTU have a second word: the preset, in ms or counts.
 */
#define PLC_WORD(op, kind, index, line) \
    ((uint32_t)(op) | (uint32_t)(kind) << 6 | (uint32_t)(index) << 8 | (uint32_t)(line) << 16)
#define PLC_OP(w)       ((w) & 0x3Fu)
#define PLC_KIND(w)     (((w) >> 6) & 0x3u)
#define PLC_INDEX(w)    (((w) >> 8) & 0xFFu)
#define PLC_LINE(w)     ((w) >> 16)

/*
 * The ladder's state. The order of the fields is fixed by PLC_LAYOUT, because
 * the viewer reads them by their position.
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
};

extern struct plc_image plc_image;

/* A program on its way from plc_asm.c to the engine. */
struct plc_program {
    uint32_t words[PLC_MAX_WORDS];
    unsigned count;                     /* words used */
    unsigned instructions;
    unsigned rungs;
    uint32_t in_mask, out_mask;
};

/* ---- plc_asm.c --------------------------------------------------------------- */

/*
 * Turn IL text into instruction words. Returns true on success; otherwise
 * 'error' says what is wrong and on which line.
 */
bool plc_assemble(const char *text, size_t len, struct plc_program *prog, char *error, size_t error_size);

/* Read an operand name such as "G4" or "m10". Returns false if it isn't one. */
bool plc_parse_operand(const char *name, size_t len, unsigned *kind, unsigned *index);

const char *plc_op_name(unsigned op);           /* "LD", "ANDN", ... */
unsigned plc_op_words(unsigned op);             /* how many words the instruction takes */
bool plc_op_is_contact(unsigned op);
bool plc_op_is_output(unsigned op);             /* OUT, SET, RST */
bool plc_op_is_box(unsigned op);                /* TON, TOF, CTU */
void plc_operand_name(uint32_t word, char *buf, size_t size);   /* "G4", "M10", "T0" */

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
 * Run a scan if one is due. The places that already poll call this: the
 * prompt's wait for a key, the Ctrl-C check while Lua runs, and delay().
 */
void plc_poll(void);

#endif
