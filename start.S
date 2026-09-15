/*
 * start.S - the first code the ESP32-C3 ROM runs in direct boot mode.
 *
 * When we get here nothing is set up: no stack, all three watchdogs are
 * running, and the CPU runs at 20 MHz (40 MHz crystal divided by 2).
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

        /* 7. Stack pointer: top of our RAM area, from the linker script */
        la   sp, _stack_top

        j    main


        .section .text, "ax"
        .align 2                            /* mtvec needs an address that is a multiple of 4 */
trap:
        j    trap
