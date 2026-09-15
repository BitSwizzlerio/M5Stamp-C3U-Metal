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

/* --- RMT, for exercise 8 (system_reg.h, rmt_reg.h, gpio_sig_map.h, gpio_reg.h) --- */
#define SYSTEM_PERIP_CLK_EN0        0x600C0010      /* clocks for peripherals */
#define SYSTEM_PERIP_RST_EN0        0x600C0018      /* resets for peripherals */
#define SYSTEM_RMT_CLK_EN           (1u << 9)
#define SYSTEM_RMT_RST              (1u << 9)
#define RMT_CH0CONF0                0x60016010      /* channel 0 (a transmit channel) */
#define RMT_TX_START_CH0            (1u << 0)       /* write 1: start sending */
#define RMT_MEM_RD_RST_CH0          (1u << 1)       /* write 1 then 0: start again from the first symbol */
#define RMT_APB_MEM_RST_CH0         (1u << 2)
#define RMT_IDLE_OUT_EN_CH0         (1u << 6)       /* drive the idle level (bit 5; 0 = low) when not sending */
#define RMT_DIV_CNT_CH0_S           8               /* bits 15:8: clock divider */
#define RMT_MEM_SIZE_CH0_S          16              /* bits 18:16: memory blocks, 48 symbols each */
#define RMT_CARRIER_EN_CH0          (1u << 21)      /* modulate a carrier (for infrared); keep off */
#define RMT_CONF_UPDATE_CH0         (1u << 24)      /* write 1: apply the settings */
#define RMT_INT_RAW                 0x60016038
#define RMT_INT_CLR                 0x60016044
#define RMT_CH0_TX_END_INT          (1u << 0)       /* channel 0 finished sending */
#define RMT_CH0_ERR_INT             (1u << 4)
#define RMT_SYS_CONF                0x60016068      /* settings for the whole RMT block */
#define RMT_APB_FIFO_MASK           (1u << 0)       /* 1: read and write the symbol memory directly */
#define RMT_SCLK_DIV_B_S            18              /* bits 23:18: fractional divider denominator */
#define RMT_SCLK_SEL_XTAL           (3u << 24)      /* bits 25:24 clock source: 1 APB, 2 RC_FAST, 3 the crystal */
#define RMT_SCLK_ACTIVE             (1u << 26)      /* clock on */
#define RMT_CLK_EN                  (1u << 31)      /* clock for the registers */
#define RMTMEM_CH0                  0x60016400      /* channel 0's symbol memory */
#define RMT_SIG_OUT0_IDX            51              /* RMT channel 0's output, for GPIO_FUNC_OUT_SEL_CFG */
#define GPIO_FUNC_OEN_SEL           (1u << 9)       /* FUNCn_OUT_SEL_CFG: take the output enable from GPIO_ENABLE */

#endif
