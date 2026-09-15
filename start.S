/*
 * start.S - the C runtime startup (crt0) for C3U-Metal.
 *
 * The ESP32-C3 ROM jumps here (0x42000008) in direct boot mode. Nothing is set
 * up yet: no stack, all three watchdogs running, the CPU at 20 MHz, and RAM full
 * of leftovers. This file gets the chip and memory ready for C, then calls main().
 */
#include "esp32c3-regs.h"

        .section .text.entry, "ax"
        .global _start
_start:
        /* 1. RTC watchdog: unlock, switch everything off (incl. flash-boot mode), lock */
        li   t0, RTC_CNTL_WDTWPROTECT
        li   t1, WDT_WKEY
        sw   t1, 0(t0)
        li   t2, RTC_CNTL_WDTCONFIG0
        sw   zero, 0(t2)
        sw   zero, 0(t0)

        /* 2. Timer Group 0 watchdog: the same, plus the bit that applies the change */
        li   t0, TIMG0_WDTWPROTECT
        sw   t1, 0(t0)                      /* same key as above */
        li   t2, TIMG0_WDTCONFIG0
        li   t3, TIMG_WDT_CONF_UPDATE_EN
        sw   t3, 0(t2)
        sw   zero, 0(t0)

        /* 3. Super watchdog: let the chip feed it automatically, as ESP-IDF does */
        li   t0, RTC_CNTL_SWD_WPROTECT
        li   t1, SWD_WKEY
        sw   t1, 0(t0)
        li   t2, RTC_CNTL_SWD_CONF
        lw   t3, 0(t2)
        li   t4, RTC_CNTL_SWD_AUTO_FEED_EN
        or   t3, t3, t4
        sw   t3, 0(t2)
        sw   zero, 0(t0)

        /* 4. CPU clock: stay on the 40 MHz crystal, but stop dividing it by 2 */
        li   t0, SYSTEM_SYSCLK_CONF
        lw   t1, 0(t0)
        li   t2, -4096                      /* 0xFFFFF000 */
        and  t1, t1, t2                     /* bits 11:0 = 0: crystal source, divider 1 */
        sw   t1, 0(t0)

        /* 5. Start the CPU cycle counter, our clock for LED pulses and delays */
        li   t0, 1
        csrw CSR_PCER, t0                   /* count CPU cycles */
        csrw CSR_PCMR, t0                   /* start counting   */

        /* 6. Send traps (bad instruction, bad address) to a loop a debugger can find */
        la   t0, trap
        csrw mtvec, t0

        /*
         * 7. Stack pointer and global pointer, both from the linker script.
         *    "norelax" stops the linker rewriting these two instructions to be
         *    relative to gp, which would use gp before it has a value.
         */
        .option push
        .option norelax
        la   sp, _stack_top
        la   gp, __global_pointer$
        .option pop

        /* 8. .data: copy the starting values from flash (read through DROM) into RAM */
        la   t0, _data_lma
        la   t1, _data_start
        la   t2, _data_end
        j    2f
1:      lw   t3, 0(t0)
        sw   t3, 0(t1)
        addi t0, t0, 4
        addi t1, t1, 4
2:      bltu t1, t2, 1b                     /* the linker script keeps both ends multiples of 4 */

        /* 9. .bss: set every zero-start variable to zero */
        la   t1, _bss_start
        la   t2, _bss_end
        j    4f
3:      sw   zero, 0(t1)
        addi t1, t1, 4
4:      bltu t1, t2, 3b

        /* 10. Run the program. main() shouldn't return; if it does, stop here. */
        call main
halt:
        j    halt


        .section .text, "ax"
        .align 2                            /* mtvec needs an address that is a multiple of 4 */
trap:
        j    trap
