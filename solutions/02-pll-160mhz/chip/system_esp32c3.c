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

#if CPU_MHZ != 160
#error "SystemInit() runs the CPU at 160 MHz from the PLL; change it together with CPU_MHZ"
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

    /* 4. CPU clock: 160 MHz from the PLL (solution to exercise 2).
     *    The PLL is already running at 480 MHz, because the USB port needs it.
     *    Choose 480 / 3 = 160 MHz first, then switch the CPU over to the PLL. */
    REG(SYSTEM_CPU_PER_CONF) = (REG(SYSTEM_CPU_PER_CONF) & ~SYSTEM_CPUPERIOD_SEL_MASK) | SYSTEM_CPUPERIOD_160M;
    REG(SYSTEM_SYSCLK_CONF) = (REG(SYSTEM_SYSCLK_CONF) & ~(SYSTEM_SOC_CLK_SEL_MASK | SYSTEM_PRE_DIV_CNT_MASK))
                            | SYSTEM_SOC_CLK_SEL_PLL;

    /* 5. Start the CPU cycle counter, used for LED pulses and delays */
    cycle_counter_start();
}
