/*
 * plc_show.c - draw the ladder program as ASCII rungs, with the power flowing.
 *
 *    0 |==[ G8 ]--+==[/G10]=====( G4 )==|  G4 = 1
 *      |          |                     |
 *      |==[ G4 ]==+                     |
 *    1 |==[ G4 ]==[TON T0 2000]-( G5 )--|  G5 = 0
 *                  1240 / 2000 ms
 *
 * "==" is a wire with power and "--" one without, as of the last scan. [ G8 ]
 * is a contact, [/G10] one that passes power while G10 is 0, ( G4 ) a coil.
 *
 * The program is a list of instructions, but a drawing needs the rung's shape.
 * IL already says what it is: AND puts a contact in series with the rung so
 * far, OR puts one in parallel, and ANB and ORB join two whole blocks. So each
 * rung is turned back into a tree of series and parallel groups, and the tree
 * is drawn into a grid of characters:
 *   - a contact is "--" then its box: 1 row high;
 *   - a series group puts its parts side by side;
 *   - a parallel group stacks its branches, one empty row apart, and joins
 *     them with '+' and '|' on the right (and on the left, unless the left
 *     rail already joins them).
 * Timers and counters follow the contacts, and the coils are lined up against
 * the right rail.
 *
 * Which wires have power comes from the trace bits the scan leaves in
 * plc_image: for each contact, whether it passed power. The drawing works out
 * the rest from those, left to right.
 *
 * Read first: app/plc_asm.c (how a rung is written) and app/plc.c (the scan).
 * Try this:   load a rung with ORB in the middle, such as
 *             LD G3 / AND G4 / LD G5 / AND G6 / ORB / OUT G7, and plc.show() it.
 */
#include <stdio.h>
#include <string.h>
#include "plc.h"

#define ROWS        40                  /* the most rows one rung may need */
#define COLS        150                 /* the widest drawing */
#define MAX_COILS   16                  /* coils and boxes one rung may have, to be drawn */
#define GREEN       "\x1b[32m"          /* ANSI colour codes, which nearly every terminal knows */
#define PLAIN       "\x1b[0m"
#define CLEAR_LINE  "\x1b[K"            /* clear to the end of the line */

enum { LEAF, SERIES, PARALLEL };

struct node {
    unsigned char type;
    bool at_rail;                       /* starts at the left rail, which joins its branches */
    unsigned short word;                /* LEAF: the contact's instruction */
    short first, last, next;            /* children, and the next sibling */
    unsigned short w, h;                /* size in characters */
};

struct rung {
    int root;                           /* the contacts */
    unsigned boxes[MAX_COILS], box_count;
    unsigned coils[MAX_COILS], coil_count;
    bool too_big;
};

/* About 22 KB, borrowed from Lua's heap only while drawing (see plc_show_workspace_size()). */
struct plc_show_workspace {
    struct node nodes[2 * PLC_MAX_WORDS];       /* each instruction makes at most a leaf and a group */
    char grid[ROWS][COLS + 1];
};

static struct plc_show_workspace *ws;
static unsigned node_count;
static bool live;                       /* the ladder has scanned since the program was loaded */

size_t plc_show_workspace_size(void)
{
    return sizeof(struct plc_show_workspace);
}


/* ---- text for each part ------------------------------------------------------- */

static bool traced(unsigned i)
{
    return live && ((plc_image.trace[i / 32] >> (i % 32)) & 1u);
}

/* "[ G8 ]", "[/G10]", "[P G8]" */
static void contact_text(unsigned i, char *buf, size_t size)
{
    uint32_t word = plc_image.program[i];
    unsigned op = PLC_OP(word);
    const char *mark = " ";
    char name[8];

    if (op == OP_LDN || op == OP_ANDN || op == OP_ORN)
        mark = "/";
    else if (op == OP_LDP || op == OP_ANDP || op == OP_ORP)
        mark = "P ";
    else if (op == OP_LDF || op == OP_ANDF || op == OP_ORF)
        mark = "F ";
    plc_operand_name(word, name, sizeof name);
    snprintf(buf, size, "[%s%-*s]", mark, (int)(4 - strlen(mark)), name);
}

/* "( G4 )", "(S G4)", "(R C0)" */
static void coil_text(unsigned i, char *buf, size_t size)
{
    uint32_t word = plc_image.program[i];
    unsigned op = PLC_OP(word);
    const char *mark = op == OP_SET ? "S " : op == OP_RST ? "R " : " ";
    char name[8];

    plc_operand_name(word, name, sizeof name);
    snprintf(buf, size, "(%s%-*s)", mark, (int)(4 - strlen(mark)), name);
}

/* "[TON T0 2000]" */
static void box_text(unsigned i, char *buf, size_t size)
{
    char name[8];

    plc_operand_name(plc_image.program[i], name, sizeof name);
    snprintf(buf, size, "[%s %s %u]", plc_op_name(PLC_OP(plc_image.program[i])), name,
             (unsigned)plc_image.program[i + 1]);
}


/* ---- from instructions to a tree -------------------------------------------- */

static int new_node(unsigned char type, unsigned word)
{
    int n = (int)node_count++;

    ws->nodes[n] = (struct node){ .type = type, .word = (unsigned short)word, .first = -1, .last = -1, .next = -1 };
    return n;
}

static void append(int parent, int child)
{
    ws->nodes[child].next = -1;
    if (ws->nodes[parent].first < 0)
        ws->nodes[parent].first = (short)child;
    else
        ws->nodes[ws->nodes[parent].last].next = (short)child;
    ws->nodes[parent].last = (short)child;
}

/* Join a and b in series or in parallel. A group of the same kind is extended, not nested. */
static int join(unsigned char type, int a, int b)
{
    int parent = a;

    if (ws->nodes[a].type != type) {
        parent = new_node(type, 0);
        append(parent, a);
    }
    if (ws->nodes[b].type == type) {
        for (int c = ws->nodes[b].first; c >= 0;) {
            int next = ws->nodes[c].next;
            append(parent, c);
            c = next;
        }
    } else {
        append(parent, b);
    }
    return parent;
}

/* Read the rung that starts at instruction i. Returns where the next one starts. */
static unsigned read_rung(unsigned i, struct rung *r)
{
    const struct plc_image *p = &plc_image;
    int stack[PLC_STACK + 1];
    int sp = 0;

    memset(r, 0, sizeof *r);
    node_count = 0;
    for (; i < p->words; i += plc_op_words(PLC_OP(p->program[i]))) {
        unsigned op = PLC_OP(p->program[i]);
        bool load = op == OP_LD || op == OP_LDN || op == OP_LDP || op == OP_LDF;

        if (load && (r->box_count || r->coil_count))
            break;                      /* the next rung */
        if (load) {
            stack[sp++] = new_node(LEAF, i);
        } else if (plc_op_is_contact(op)) {
            bool series = op == OP_AND || op == OP_ANDN || op == OP_ANDP || op == OP_ANDF;
            stack[sp - 1] = join(series ? SERIES : PARALLEL, stack[sp - 1], new_node(LEAF, i));
        } else if (op == OP_ANB || op == OP_ORB) {
            int b = stack[--sp];
            stack[sp - 1] = join(op == OP_ANB ? SERIES : PARALLEL, stack[sp - 1], b);
        } else if (plc_op_is_box(op)) {
            if (r->box_count < MAX_COILS)
                r->boxes[r->box_count++] = i;
            else
                r->too_big = true;
        } else {
            if (r->coil_count < MAX_COILS)
                r->coils[r->coil_count++] = i;
            else
                r->too_big = true;
        }
    }
    r->root = stack[0];
    return i;
}


/* ---- laying out and drawing the tree ------------------------------------------ */

static void measure(int n, bool at_rail)
{
    struct node *d = &ws->nodes[n];
    unsigned w = 0, h = 0, count = 0;

    d->at_rail = at_rail;
    if (d->type == LEAF) {
        char text[16];
        contact_text(d->word, text, sizeof text);
        d->w = (unsigned short)(2 + strlen(text));
        d->h = 1;
        return;
    }
    for (int c = d->first; c >= 0; c = ws->nodes[c].next, count++) {
        measure(c, d->type == SERIES ? (at_rail && count == 0) : at_rail);
        if (d->type == SERIES) {
            w += ws->nodes[c].w;
            h = ws->nodes[c].h > h ? ws->nodes[c].h : h;
        } else {
            w = ws->nodes[c].w > w ? ws->nodes[c].w : w;
            h += ws->nodes[c].h + (count ? 1 : 0);
        }
    }
    if (d->type == PARALLEL)
        w += (at_rail ? 0 : 3) + 3;     /* "--" and the left junction, "--" to line the branches up, right junction */
    d->w = (unsigned short)w;
    d->h = (unsigned short)h;
}

static void put(unsigned row, unsigned col, char c)
{
    if (row < ROWS && col < COLS)
        ws->grid[row][col] = c;
}

static void text(unsigned row, unsigned col, const char *s)
{
    while (*s)
        put(row, col++, *s++);
}

static void wire(unsigned row, unsigned col, unsigned len, bool power)
{
    while (len-- > 0)
        put(row, col++, power ? '=' : '-');
}

/* Draw node n with its top left at (row, col), fed with 'power'. Returns the power coming out. */
static bool draw(int n, unsigned row, unsigned col, bool power)
{
    const struct node *d = &ws->nodes[n];

    if (d->type == LEAF) {
        char box[16];
        contact_text(d->word, box, sizeof box);
        wire(row, col, 2, power);
        text(row, col + 2, box);
        return power && traced(d->word);
    }
    if (d->type == SERIES) {
        for (int c = d->first; c >= 0; c = ws->nodes[c].next) {
            power = draw(c, row, col, power);
            col += ws->nodes[c].w;
        }
        return power;
    }

    /* A parallel group away from the rail starts with "--" and its own left junction. */
    unsigned left = d->at_rail ? 0 : 3;
    unsigned junction = col + 2;        /* the left junction's column, when there is one */
    unsigned right = col + d->w - 1;    /* the right junction's column */
    unsigned first = row, last = row;
    bool out = false;
    if (left)
        wire(row, col, 2, power);
    for (int c = d->first; c >= 0; c = ws->nodes[c].next) {
        bool p = draw(c, row, col + left, power);
        wire(row, col + left + ws->nodes[c].w, right - (col + left + ws->nodes[c].w), p);
        put(row, right, '+');
        if (left)
            put(row, junction, '+');
        out = out || p;
        last = row;
        row += ws->nodes[c].h + 1;
    }
    for (unsigned r = first + 1; r < last; r++) {
        if (ws->grid[r][right] == ' ')
            put(r, right, '|');
        if (left && ws->grid[r][junction] == ' ')
            put(r, junction, '|');
    }
    return out;
}

/* The width a rung needs, from rail to rail. */
static unsigned rung_width(const struct rung *r)
{
    unsigned w = 1 + ws->nodes[r->root].w + 2 + 2 + 1;      /* rails, the root, the shortest wire before the coils and after */
    unsigned widest = 0;
    char buf[32];

    for (unsigned k = 0; k < r->box_count; k++) {
        box_text(r->boxes[k], buf, sizeof buf);
        w += 2 + (unsigned)strlen(buf);
    }
    for (unsigned k = 0; k < r->coil_count; k++) {
        coil_text(r->coils[k], buf, sizeof buf);
        widest = strlen(buf) > widest ? (unsigned)strlen(buf) : widest;
    }
    return w + widest + (r->coil_count > 1 ? 3 : 0);       /* "--" and the coils' junction */
}


/* ---- printing --------------------------------------------------------------- */

/* Print a row with the wires that have power in green. */
static void print_row(const char *prefix, const char *row, const char *after, bool colour)
{
    bool green = false;

    fputs(prefix, stdout);
    for (const char *c = row; *c; c++) {
        bool power = (*c == '=');
        if (colour && power != green) {
            fputs(power ? GREEN : PLAIN, stdout);
            green = power;
        }
        putchar(*c);
    }
    if (green)
        fputs(PLAIN, stdout);
    fputs(after, stdout);
    if (colour)
        fputs(CLEAR_LINE, stdout);          /* clear the rest of the line, for plc.watch() */
    putchar('\n');
}

/* The value a coil's operand has now: "  G4 = 1". */
static void coil_value(unsigned i, char *buf, size_t size)
{
    const struct plc_image *p = &plc_image;
    uint32_t word = p->program[i];
    unsigned n = PLC_INDEX(word);
    char name[8];

    buf[0] = '\0';
    if (!live)
        return;
    plc_operand_name(word, name, sizeof name);
    if (PLC_KIND(word) == KIND_G)
        snprintf(buf, size, "  %s = %u", name, (unsigned)((p->outputs >> n) & 1u));
    else if (PLC_KIND(word) == KIND_M)
        snprintf(buf, size, "  %s = %u", name, (unsigned)((p->m[n / 32] >> (n % 32)) & 1u));
}

/* Draw one rung, 'width' characters from rail to rail. Returns the lines printed. */
static unsigned show_rung(const struct rung *r, unsigned number, unsigned width, bool colour)
{
    const struct plc_image *p = &plc_image;
    unsigned rows = ws->nodes[r->root].h > r->coil_count ? ws->nodes[r->root].h : r->coil_count;
    unsigned box_col[MAX_COILS];
    char buf[40], prefix[8];

    for (unsigned row = 0; row < rows; row++) {
        memset(ws->grid[row], ' ', width);
        ws->grid[row][width] = '\0';
        ws->grid[row][0] = ws->grid[row][width - 1] = '|';
    }

    bool power = draw(r->root, 0, 1, live);             /* the left rail always has power */
    unsigned col = 1 + ws->nodes[r->root].w;
    for (unsigned k = 0; k < r->box_count; k++) {       /* timers and counters, in series */
        box_text(r->boxes[k], buf, sizeof buf);
        wire(0, col, 2, power);
        box_col[k] = col + 2;
        text(0, col + 2, buf);
        col += 2 + (unsigned)strlen(buf);
        power = traced(r->boxes[k]);
    }
    if (r->coil_count == 0)
        wire(0, col, width - 1 - col, power);
    for (unsigned k = 0; k < r->coil_count; k++) {      /* coils, lined up on the right */
        unsigned start = col;
        if (r->coil_count > 1) {        /* several coils: "--", then a junction feeding one per row */
            wire(0, col, 2, power);
            put(k, col + 2, '+');
            start = col + 3;
        }
        coil_text(r->coils[k], buf, sizeof buf);
        unsigned at = width - 3 - (unsigned)strlen(buf);
        wire(k, start, at - start, power);
        text(k, at, buf);
        wire(k, at + (unsigned)strlen(buf), 2, traced(r->coils[k]));
    }

    for (unsigned row = 0; row < rows; row++) {
        char after[24] = "";
        if (row < r->coil_count)
            coil_value(r->coils[row], after, sizeof after);
        snprintf(prefix, sizeof prefix, row == 0 ? "%2u " : "   ", number);
        print_row(prefix, ws->grid[row], after, colour);
    }
    if (!live || r->box_count == 0)
        return rows;

    /* Under each timer or counter: how far it has got. */
    memset(ws->grid[0], ' ', width);
    ws->grid[0][width] = '\0';
    for (unsigned k = 0; k < r->box_count; k++) {
        uint32_t word = p->program[r->boxes[k]];
        unsigned n = PLC_INDEX(word);
        if (PLC_OP(word) == OP_CTU)
            snprintf(buf, sizeof buf, "%u / %u", (unsigned)p->c[n].count, (unsigned)p->program[r->boxes[k] + 1]);
        else
            snprintf(buf, sizeof buf, "%u / %u ms", (unsigned)(p->t[n].elapsed_us / 1000u),
                     (unsigned)p->program[r->boxes[k] + 1]);
        text(0, box_col[k] + 1, buf);
    }
    size_t end = strlen(ws->grid[0]);
    while (end > 0 && ws->grid[0][end - 1] == ' ')
        ws->grid[0][--end] = '\0';
    print_row("   ", ws->grid[0], "", colour);
    return rows + 1;
}

unsigned plc_show(void *workspace, bool colour)
{
    const struct plc_image *p = &plc_image;
    struct rung r;
    unsigned width = 30, lines = 0, number = 0;

    ws = workspace;
    live = p->scans > 0;
    for (unsigned i = 0; i < p->words; number++) {      /* first, how wide the widest rung is */
        i = read_rung(i, &r);
        measure(r.root, true);
        unsigned w = rung_width(&r);
        width = w > width ? w : width;
    }
    if (width > COLS)
        width = COLS;

    number = 0;
    for (unsigned i = 0; i < p->words; number++) {
        i = read_rung(i, &r);
        measure(r.root, true);
        if (r.too_big || rung_width(&r) > COLS || ws->nodes[r.root].h > ROWS) {
            printf("%2u  (this rung is too big to draw; plc.list() shows it)\n", number);
            lines++;
            continue;
        }
        lines += show_rung(&r, number, width, colour);
    }
    return lines;
}
