/*
 * trap.S - where the CPU goes when something goes wrong.
 *
 * A trap is the CPU abandoning what it was doing because of an exception (an
 * illegal instruction, a read from an address with no memory, ...) or an
 * interrupt. The CPU puts the address of the instruction in mepc and the
 * reason in mcause, then jumps to the address in the mtvec register. crt0.S
 * points mtvec at trap_vector_table.
 *
 * The ESP32-C3 uses mtvec in "vectored" mode: exceptions go to the first entry
 * of the table and interrupt n goes to entry n. Its CPU ignores the low 8 bits
 * of the table's address, so the table must start at a multiple of 256. ESP-IDF
 * does the same in components/riscv/vectors_intc.S.
 *
 * This program never enables interrupts, so every entry leads to the crash
 * report in trap_report.c.
 *
 * Read first: boot/crt0.S (step 3 points mtvec here).
 * Try this:   type crash() at the console, then look up the ra address it prints:
 *             riscv32-esp-elf-addr2line -f -e build/c3u-metal.elf <address>
 */

        .section .text, "ax"

        .balign 256
        .global trap_vector_table
trap_vector_table:
        .option push
        .option norvc                       /* every entry must be exactly 4 bytes */
        .option norelax
        .rept 32
        j    trap_entry
        .endr
        .option pop


/*
 * The crash handler. The program is in an unknown state: sp or gp may be what
 * caused the trap. So: keep the old sp in mscratch, switch to a small stack of
 * our own, reload gp, and call C with the facts as arguments. It never returns.
 */
        .balign 4
trap_entry:
        csrw mscratch, sp                   /* remember sp as it was at the trap */
        .option push
        .option norelax                     /* these must not be made relative to gp */
        la   sp, trap_stack_top
        la   gp, __global_pointer$
        .option pop
        csrr a0, mcause                     /* why */
        csrr a1, mepc                       /* the instruction that trapped */
        csrr a2, mtval                      /* the bad address or instruction, if any */
        csrr a3, mscratch                   /* sp at the trap */
        mv   a4, ra                         /* where the trapping function was called from (usually) */
        call trap_report
1:      j    1b                             /* stop here; a debugger can still look around */


        .section .bss
        .balign 16
trap_stack:
        .skip 2048
trap_stack_top:
