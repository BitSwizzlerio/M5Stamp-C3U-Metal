/*
 * trap_report.c - print a crash report when the CPU traps (see chip/trap.S).
 *
 * Nothing here uses printf or malloc: the crash may have been caused by
 * damaged memory, so the report uses as little of the program as it can.
 *
 * Read first: chip/trap.S.
 * Solution:   exercise 1c also prints mstatus, passed in a5 by chip/trap.S.
 */
#include <stdint.h>
#include <string.h>
#include "linker_symbols.h"
#include "usb_serial.h"

#define MCAUSE_INTERRUPT    0x80000000u     /* bit 31 set: an interrupt, not an exception */

/* Exception codes from the RISC-V privileged specification, in plain words. */
static const char *const exception_names[] = {
    [0]  = "instruction address misaligned",
    [1]  = "instruction access fault: jumped to an address where there is no code",
    [2]  = "illegal instruction: the bytes at mepc are not an instruction",
    [3]  = "breakpoint (ebreak)",
    [4]  = "load address misaligned",
    [5]  = "load access fault: read from an address where there is no memory",
    [6]  = "store address misaligned",
    [7]  = "store access fault: wrote to an address where there is no writable memory",
    [8]  = "environment call (ecall) from user mode",
    [11] = "environment call (ecall)",
};

static void put_str(const char *s)
{
    usb_serial_write(s, strlen(s));
}

/* Print "label0x12345678   note" on one line. */
static void put_hex(const char *label, uint32_t value, const char *note)
{
    char hex[] = "0x00000000";

    for (int i = 0; i < 8; i++)
        hex[9 - i] = "0123456789abcdef"[(value >> (4 * i)) & 0xF];
    put_str(label);
    put_str(hex);
    put_str("   ");
    put_str(note);
    put_str("\n");
}

void trap_report(uint32_t mcause, uint32_t mepc, uint32_t mtval, uint32_t sp, uint32_t ra, uint32_t mstatus)
{
    uint32_t code = mcause & ~MCAUSE_INTERRUPT;

    put_str("\n\n*** CPU trap: ");
    if (mcause & MCAUSE_INTERRUPT)
        put_str("an interrupt, but none were enabled");
    else if (code < sizeof exception_names / sizeof exception_names[0] && exception_names[code])
        put_str(exception_names[code]);
    else
        put_str("unknown exception");
    put_str(" ***\n");

    put_hex("mcause  ", mcause, "the reason, as a number");
    put_hex("mepc    ", mepc, "address of the instruction that trapped");
    put_hex("mtval   ", mtval, "the bad address or instruction (0 if none)");
    put_hex("ra      ", ra, "return address: usually where that function was called from");
    put_hex("sp      ", sp, "stack pointer");
    put_hex("mstatus ", mstatus, "CPU status: bits 12:11 = 3 means it was in machine mode");
    if (sp < (uint32_t)_stack_bottom || sp > (uint32_t)_stack_top)
        put_str("sp is outside the stack: a stack overflow is likely\n");

    put_str("\nTo see which source line an address is in:\n"
            "  riscv32-esp-elf-addr2line -f -e build/c3u-metal.elf <address>\n"
            "The program has stopped. Unplug the board and plug it back in to restart.\n");
}
