/*
 * fuzz_asm.c - feed the ladder assembler whatever libFuzzer can think of.
 *
 * libFuzzer (part of clang) calls LLVMFuzzerTestOneInput() millions of times,
 * each with a different input, and keeps the ones that reach new code in the
 * assembler. Starting from the example programs and the words in fuzz.dict,
 * it soon writes programs no person would. Every input is assembled; one that
 * assembles is installed, scanned a few times with the inputs changing, and
 * drawn. AddressSanitizer stops the run at the first byte read or written out
 * of bounds, and prints the input that did it.
 *
 *     python sim/build.py fuzz [seconds]      (clang on Linux; GitHub Actions runs it)
 *
 * test_ladder.c's mangled programs do something similar on any PC, but blindly;
 * this finds its way into the corners.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "plc.h"
#include "sim.h"

static void *workspace;

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (!freopen("/dev/null", "w", stdout))     /* the drawings: thousands a second, not worth keeping */
        return 1;
    workspace = malloc(plc_show_workspace_size());
    sim_reset();
    plc_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct plc_program prog;
    char error[200];

    if (size >= PLC_SOURCE_MAX)                 /* plc.load() refuses these before the assembler sees them */
        return 0;
    if (!plc_assemble((const char *)data, size, &prog, error, sizeof error))
        return 0;
    plc_install(&prog, (const char *)data, size);
    for (int scan = 0; scan < 4; scan++) {
        sim_set_input(9, scan & 1 ? 0 : -1);   /* the button, pressed and let go */
        sim_set_input(10, scan >> 1);
        sim_advance_us(7000);
        plc_step();
    }
    plc_show(workspace, size & 1);
    plc_stop();
    return 0;
}
