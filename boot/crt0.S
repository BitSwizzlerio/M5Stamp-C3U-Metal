/*
 * crt0.S - the C runtime startup for C3U-Metal.
 *
 * The ESP32-C3 ROM jumps here (0x42000008) in direct boot mode. Following the
 * Arm CMSIS pattern, this file only does C-runtime work and calls SystemInit()
 * (chip/system_esp32c3.c) for everything specific to the chip:
 *
 *   1. sp and gp               so C functions can run
 *   2. fill the stack          with STACK_FILL, so stack_check.c can measure its use
 *   3. mtvec                   traps go to the crash report (chip/trap.S)
 *   4. SystemInit()            watchdogs, clock, cycle counter (no global variables yet)
 *   5. RAM code, .data, .bss   copy code and starting values from flash; zero the rest
 *   6. __libc_init_array()     newlib runs any start-up functions (constructors)
 *   7. main()
 */
#include "stack_check.h"

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

        /* 2. Fill the whole stack with STACK_FILL. Nothing has been pushed yet. */
        la   t0, _stack_bottom
        li   t1, STACK_FILL
        j    8f
7:      sw   t1, 0(t0)
        addi t0, t0, 4
8:      bltu t0, sp, 7b

        /* 3. Traps (bad instruction, bad address) go to the crash report. +1 selects vectored mode. */
        la   t0, trap_vector_table
        ori  t0, t0, 1
        csrw mtvec, t0

        /* 4. Chip setup, in C. It runs before step 5, so it must not use global variables. */
        call SystemInit

        /*
         * 5a. RAM code (.iram.text): copy it from flash into RAM. It is written
         *     through the RAM's data address and later runs at its code address;
         *     both are the same memory.
         */
        la   t0, _iram_text_lma
        la   t1, _iram_text_dram
        la   t2, _iram_text_start
        la   t3, _iram_text_end
        sub  t3, t3, t2                     /* t3 = bytes to copy */
        add  t2, t1, t3                     /* t2 = end address, as a data address */
        j    6f
5:      lw   t4, 0(t0)
        sw   t4, 0(t1)
        addi t0, t0, 4
        addi t1, t1, 4
6:      bltu t1, t2, 5b

        /* 5b. .data: copy the starting values from flash (read through DROM) into RAM */
        la   t0, _data_lma
        la   t1, _data_start
        la   t2, _data_end
        j    2f
1:      lw   t3, 0(t0)
        sw   t3, 0(t1)
        addi t0, t0, 4
        addi t1, t1, 4
2:      bltu t1, t2, 1b                     /* the linker script keeps both ends multiples of 4 */

        /* 5c. .bss: set every zero-start variable to zero */
        la   t1, _bss_start
        la   t2, _bss_end
        j    4f
3:      sw   zero, 0(t1)
        addi t1, t1, 4
4:      bltu t1, t2, 3b

        /* 6. Start-up functions listed in .init_array (none today, but C++ would need them) */
        call __libc_init_array

        /* 7. Run the program. main() shouldn't return; if it does, stop here. */
        call main
halt:
        j    halt
