/*
 * esp32c3-regs.h - the ESP32-C3 hardware registers this project uses.
 *
 * Addresses and bit positions were checked against ESP-IDF v6.0.1:
 * components/soc/esp32c3/register/soc/{reg_base,rtc_cntl_reg,timer_group_reg,
 * system_reg,io_mux_reg,gpio_reg}.h and components/esp_hal_wdt/.../rwdt_ll.h.
 *
 * Try this: find GPIO_IN (0x6000403C) in Espressif's ESP32-C3 Technical Reference
 *           Manual, then read it live at the console: hex(peek(0x6000403C)).
 *           Bit 9 changes while you hold the button.
 */
#ifndef ESP32C3_REGS_H
#define ESP32C3_REGS_H

/*
 * Shared by the assembly (.S) and C (.c) files, so everything below is a plain
 * #define. C files also get REG(); GCC defines __ASSEMBLER__ for .S files, which
 * keeps C-only code away from the assembler.
 */
#ifndef __ASSEMBLER__
#include <stdint.h>
#define REG(addr)   (*(volatile uint32_t *)(addr))  /* read or write a 32-bit hardware register */
#endif

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
#define IO_MUX_GPIO(n)              (0x60009004 + 4 * (n))  /* the pad register for GPIOn */
#define IO_MUX_MCU_SEL_GPIO        (1 << 12)       /* bits 14:12 function select; 1 = GPIO */
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
#define GPIO_FUNC_OUT_SEL_CFG(n)    (0x60004554 + 4 * (n))  /* which signal drives GPIOn */
#define SIG_GPIO_OUT_IDX            128             /* signal number for "plain GPIO output" */

/* --- USB Serial/JTAG block at 0x60043000 (usb_serial_jtag_reg.h) --- */
#define USB_SERIAL_EP1              0x60043000      /* read: next received byte; write: add a byte to send */
#define USB_SERIAL_EP1_CONF         0x60043004
#define USB_SERIAL_WR_DONE          (1 << 0)        /* write 1: send the buffered bytes now */
#define USB_SERIAL_TX_FREE          (1 << 1)        /* reads 1: room in the send buffer (SERIAL_IN_EP_DATA_FREE) */
#define USB_SERIAL_RX_AVAIL         (1 << 2)        /* reads 1: a received byte is waiting (SERIAL_OUT_EP_DATA_AVAIL) */

/* --- SYSTIMER block at 0x60023000 (systimer_reg.h), for exercise 3 --- */
#define SYSTIMER_CONF               0x60023000
#define SYSTIMER_CLK_EN             (1u << 31)      /* clock for the registers; ESP-IDF always sets it */
#define SYSTIMER_UNIT0_OP           0x60023004
#define SYSTIMER_UNIT0_UPDATE       (1u << 30)      /* write 1: take a snapshot of counter unit 0 */
#define SYSTIMER_UNIT0_VALUE_VALID  (1u << 29)      /* reads 1 when the snapshot is ready */
#define SYSTIMER_UNIT0_VALUE_HI     0x60023040      /* snapshot, bits 51:32 */
#define SYSTIMER_UNIT0_VALUE_LO     0x60023044      /* snapshot, bits 31:0 */
#define SYSTIMER_TICKS_PER_US       16u             /* 16 000 000 counts a second, from the crystal */

#define USB_SERIAL_BLOCK            0x60043000      /* the whole block, 0x60043000 to 0x60043FFF: poke() refuses it */

/* --- Software reset: RTC_CNTL block at 0x60008000 --- */
#define RTC_CNTL_OPTIONS0           0x60008000      /* chip-wide switches */
#define RTC_CNTL_SW_PROCPU_RST      (1 << 5)        /* bit 5: restart the CPU, leaving the rest of the chip alone */

/* --- Random numbers: SYSCON block at 0x60026000 --- */
#define RNG_DATA                    0x600260B0      /* a different value on every read; app/main.c seeds math.random with it */

#endif
