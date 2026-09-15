/*
 * esp32c3-regs.h - the ESP32-C3 hardware registers this project uses.
 *
 * Addresses and bit positions were checked against ESP-IDF v6.0.1:
 * components/soc/esp32c3/register/soc/{reg_base,rtc_cntl_reg,timer_group_reg,
 * system_reg,io_mux_reg,gpio_reg}.h and components/esp_hal_wdt/.../rwdt_ll.h.
 */
#ifndef ESP32C3_REGS_H
#define ESP32C3_REGS_H

/* --- Watchdogs: RTC_CNTL block at 0x60008000, Timer Group 0 at 0x6001F000 --- */
#define RTC_CNTL_WDTCONFIG0         0x60008090      /* RTC watchdog config; bit 31 enable, bit 12 flash-boot mode */
#define RTC_CNTL_WDTWPROTECT        0x600080A8      /* write WDT_WKEY to unlock, anything else to lock */
#define RTC_CNTL_SWD_CONF           0x600080AC      /* super watchdog config */
#define RTC_CNTL_SWD_WPROTECT       0x600080B0      /* write SWD_WKEY to unlock */
#define RTC_CNTL_SWD_AUTO_FEED_EN   0x80000000      /* bit 31: the chip feeds the super watchdog itself */
#define TIMG0_WDTCONFIG0            0x6001F048      /* Timer Group 0 watchdog; bit 31 enable, bit 14 flash-boot mode */
#define TIMG0_WDTWPROTECT           0x6001F064      /* write WDT_WKEY to unlock */
#define TIMG_WDT_CONF_UPDATE_EN     0x00400000      /* bit 22: apply the new watchdog config */
#define WDT_WKEY                    0x50D83AA1      /* unlock key for the RTC and Timer Group watchdogs */
#define SWD_WKEY                    0x8F1D312A      /* unlock key for the super watchdog */

/* --- Clock: SYSTEM block at 0x600C0000 --- */
#define SYSTEM_SYSCLK_CONF          0x600C0058      /* bits 11:10 CPU clock source (0 = crystal), bits 9:0 divider - 1 */

/* --- CPU cycle counter: Espressif-specific CSRs --- */
#define CSR_PCER                    0x7E0           /* event select: 1 = count cycles */
#define CSR_PCMR                    0x7E1           /* mode: 1 = counting on */
#define CSR_PCCR                    0x7E2           /* the count */

/* --- Pads: IO_MUX block at 0x60009000, one register per pin --- */
#define IO_MUX_GPIO2                0x6000900C
#define IO_MUX_GPIO9                0x60009028
#define IO_MUX_MCU_SEL_GPIO         (1 << 12)       /* bits 14:12 function select; 1 = GPIO */
#define IO_MUX_FUN_IE               (1 << 9)        /* input enable */
#define IO_MUX_FUN_PU               (1 << 8)        /* pull-up */
#define IO_MUX_FUN_PD               (1 << 7)        /* pull-down */
#define IO_MUX_CLEAR_MASK           0x7380          /* function select, input enable, pull-up and pull-down */

/* --- GPIO block at 0x60004000 --- */
#define GPIO_OUT_W1TS               0x60004008      /* write 1 to drive pins high */
#define GPIO_OUT_W1TC               0x6000400C      /* write 1 to drive pins low */
#define GPIO_ENABLE_W1TS            0x60004024      /* write 1 to turn a pin's output driver on */
#define GPIO_ENABLE_W1TC            0x60004028      /* write 1 to turn a pin's output driver off */
#define GPIO_IN                     0x6000403C      /* current level of every pin */
#define GPIO_FUNC2_OUT_SEL_CFG      0x6000455C      /* which signal drives GPIO2 */
#define SIG_GPIO_OUT_IDX            128             /* signal number for "plain GPIO output" */

#endif
