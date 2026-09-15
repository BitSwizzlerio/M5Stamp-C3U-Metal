/*
 * system_esp32c3.c - SystemInit(): get the ESP32-C3 ready to run the program.
 *
 * Named after the Arm CMSIS convention (system_<device>.c). crt0.S calls this
 * right after setting sp and gp, before .data is copied and .bss is cleared,
 * so it must not use global variables. Local variables and function calls
 * are fine, because the stack is already set up.
 *
 * On entry all three watchdogs are running and the CPU is at 20 MHz
 * (40 MHz crystal divided by 2).
 *
 * Read first: boot/crt0.S (which calls this) and esp32c3-regs.h.
 * Try this:   comment out step 1 or step 2, rebuild and flash. The watchdog
 *             the ROM started resets the board after a while; the console drops out.
 */
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"

#if CPU_MHZ != 40
#error "SystemInit() only knows how to run the CPU at 40 MHz (the crystal, divided by 1)"
#endif

void SystemInit(void)
{
    /* 1. RTC watchdog: unlock, switch everything off (incl. flash-boot mode), lock */
    REG(RTC_CNTL_WDTWPROTECT) = WDT_WKEY;
    REG(RTC_CNTL_WDTCONFIG0) = 0;
    REG(RTC_CNTL_WDTWPROTECT) = 0;

    /* 2. Timer Group 0 watchdog: the same, plus the bit that applies the change */
    REG(TIMG0_WDTWPROTECT) = WDT_WKEY;
    REG(TIMG0_WDTCONFIG0) = TIMG_WDT_CONF_UPDATE_EN;
    REG(TIMG0_WDTWPROTECT) = 0;

    /* 3. Super watchdog: let the chip feed it automatically, as ESP-IDF does */
    REG(RTC_CNTL_SWD_WPROTECT) = SWD_WKEY;
    REG(RTC_CNTL_SWD_CONF) |= RTC_CNTL_SWD_AUTO_FEED_EN;
    REG(RTC_CNTL_SWD_WPROTECT) = 0;

    /* 4. CPU clock: stay on the 40 MHz crystal, but stop dividing it by 2.
     *    Bits 11:10 = 0 selects the crystal; bits 9:0 = 0 means divide by 1. */
    REG(SYSTEM_SYSCLK_CONF) &= ~0xFFFu;

    /* 5. Start the CPU cycle counter, used for LED pulses */
    cycle_counter_start();

    /* 6. SYSTIMER (solution to exercise 3): it counts from reset, but also turn
     *    on the clock for its registers, as ESP-IDF does. */
    REG(SYSTIMER_CONF) |= SYSTIMER_CLK_EN;
}
