/*
 * wasm_api.c - what the web simulator (viewer/c3uview/web/sim.js) calls in the WebAssembly
 * build of the ladder engine. The engine is the board's own C (app/plc.c and
 * plc_asm.c) on the simulated chip in sim/hw.c; this file only exports a few
 * functions for JavaScript to call.
 *
 * JavaScript reads plc_image straight out of the WebAssembly memory, at the
 * address sim_image() gives, with the same offsets the C3U Viewer uses to read
 * it from a real board over JTAG (viewer/c3uview/ladder.py). It writes the
 * viewer's commands into plc_image.command the same way, and plc_poll() carries
 * them out between scans, as on the board.
 */
#include <string.h>
#include "plc.h"
#include "sim.h"

static char text[PLC_SOURCE_MAX];               /* JavaScript writes a program's IL here, then calls sim_load() */
static char error[200];
static struct plc_program prog;

int   sim_api_version(void)  { return 1; }
void *sim_image(void)        { return &plc_image; }
int   sim_image_size(void)   { return (int)sizeof plc_image; }
char *sim_text(void)         { return text; }
int   sim_text_size(void)    { return (int)sizeof text; }
const char *sim_error(void)  { return error; }
int   sim_instructions(void) { return (int)prog.instructions; }
int   sim_rungs(void)        { return (int)prog.rungs; }

/* As plc.load(): assemble the first len bytes of text and install the program. 1, or 0 and sim_error(). */
int sim_load(int len)
{
    error[0] = '\0';
    if (len < 0 || len >= (int)sizeof text) {
        strcpy(error, "the program is too long");
        return 0;
    }
    if (!plc_assemble(text, (size_t)len, &prog, error, sizeof error))
        return 0;
    plc_install(&prog, text, (size_t)len);
    sim_settle();
    return 1;
}

void sim_run(int ms)          { plc_run((uint32_t)ms * 1000u); sim_settle(); }
void sim_stop(void)           { plc_stop(); sim_settle(); }
void sim_step(void)           { sim_settle(); plc_step(); sim_settle(); }
void sim_reset_board(void)    { sim_reset(); plc_init(); }

/* Move the clock on, calling plc_poll() every millisecond as the board's console does while it waits. */
void sim_advance_ms(int ms)
{
    while (ms-- > 0) {
        sim_advance_us(1000);
        sim_settle();
        plc_poll();
        sim_settle();
    }
}
