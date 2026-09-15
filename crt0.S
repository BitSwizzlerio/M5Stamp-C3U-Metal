/*
 * crt0.S - the C runtime startup for C3U-Metal.
 *
 * The ESP32-C3 ROM jumps here (0x42000008) in direct boot mode. Following the
 * Arm CMSIS pattern, this file only does C-runtime work and calls SystemInit()
 * (system_esp32c3.c) for everything specific to the chip:
 *
 *   1. sp and gp       so C functions can run
 *   2. mtvec           traps go somewhere a debugger can find
 *   3. SystemInit()    watchdogs, clock, cycle counter (no global variables yet)
 *   4. .data, .bss     global variables get their starting values
 *   5. main()
 */

        .section .text.entry, "ax"
        .global _start
_start:
        /*
         * 1. Stack pointer and global pointer, both from the linker script.
         *    "norelax" stops the linker rewriting these two instructions to be
         *    relative to gp, which would use gp before it has a value.
         */
        .option push
        .option norelax
        la   sp, _stack_top
        la   gp, __global_pointer$
        .option pop

        /* 2. Send traps (bad instruction, bad address) to a loop a debugger can find */
        la   t0, trap
        csrw mtvec, t0

        /* 3. Chip setup, in C. It runs before step 4, so it must not use global variables. */
        call SystemInit

        /* 4a. .data: copy the starting values from flash (read through DROM) into RAM */
        la   t0, _data_lma
        la   t1, _data_start
        la   t2, _data_end
        j    2f
1:      lw   t3, 0(t0)
        sw   t3, 0(t1)
        addi t0, t0, 4
        addi t1, t1, 4
2:      bltu t1, t2, 1b                     /* the linker script keeps both ends multiples of 4 */

        /* 4b. .bss: set every zero-start variable to zero */
        la   t1, _bss_start
        la   t2, _bss_end
        j    4f
3:      sw   zero, 0(t1)
        addi t1, t1, 4
4:      bltu t1, t2, 3b

        /* 5. Run the program. main() shouldn't return; if it does, stop here. */
        call main
halt:
        j    halt


        .section .text, "ax"
        .align 2                            /* mtvec needs an address that is a multiple of 4 */
trap:
        j    trap
