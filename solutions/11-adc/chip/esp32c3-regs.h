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
#define USB_SERIAL_BLOCK            0x60043000      /* the whole block, 0x60043000 to 0x60043FFF: poke() refuses it */

/* --- Software reset: RTC_CNTL block at 0x60008000 --- */
#define RTC_CNTL_OPTIONS0           0x60008000      /* chip-wide switches */
#define RTC_CNTL_SW_PROCPU_RST      (1 << 5)        /* bit 5: restart the CPU, leaving the rest of the chip alone */

/* --- Random numbers: SYSCON block at 0x60026000 --- */
#define RNG_DATA                    0x600260B0      /* a different value on every read; app/main.c seeds math.random with it */

/* --- SAR ADC, for exercise 11 (system_reg.h, apb_saradc_reg.h, efuse_reg.h, regi2c_ctrl_ll.h) --- */
#define SYSTEM_PERIP_CLK_EN0        0x600C0010      /* clocks for peripherals */
#define SYSTEM_PERIP_RST_EN0        0x600C0018      /* resets for peripherals */
#define SYSTEM_APB_SARADC_CLK_EN    (1u << 28)
#define SYSTEM_APB_SARADC_RST       (1u << 28)
#define APB_SARADC_CTRL             0x60040000      /* the ADC controller */
#define APB_SARADC_SAR_CLK_GATED    (1u << 6)       /* clock the ADC */
#define APB_SARADC_SAR_CLK_DIV_S    7               /* bits 14:7: the ADC's clock, from the controller's */
#define APB_SARADC_XPD_SAR_ON       (3u << 27)      /* bits 28:27: 3 = powered on by software */
#define APB_SARADC_CTRL_CLEAR       ((3u << 27) | (0xFFu << 7) | (1u << 6))
#define APB_SARADC_ONETIME_SAMPLE   0x60040020      /* one conversion at a time */
#define APB_SARADC_ADC1_ONETIME     (1u << 31)      /* use ADC1 */
#define APB_SARADC_ONETIME_START    (1u << 29)      /* 0 then 1: convert */
#define APB_SARADC_ONETIME_CHANNEL_S 25             /* bits 28:25: unit * 8 + channel */
#define APB_SARADC_ONETIME_ATTEN_S  23              /* bits 24:23: attenuation 0-3 */
#define APB_SARADC_1_DATA_STATUS    0x6004002C      /* bits 11:0: ADC1's last result */
#define APB_SARADC_INT_RAW          0x60040044
#define APB_SARADC_INT_CLR          0x6004004C
#define APB_SARADC_ADC1_DONE        (1u << 31)      /* ADC1 has finished a conversion */
#define APB_SARADC_CLKM_CONF        0x60040054      /* the controller's clock */
#define APB_SARADC_CLK_SEL_APB      (2u << 21)      /* bits 22:21: from APB */
#define APB_SARADC_CLK_EN           (1u << 20)
#define APB_SARADC_CLKM_DIV_B_S     8               /* bits 13:8, and DIV_A in 19:14: a fraction a/b */
#define APB_SARADC_CLKM_DIV_NUM_S   0               /* bits 7:0: divide by this + 1 + a/b */
#define ANA_CONFIG                  0x6000E044      /* the analog bus inside the chip */
#define ANA_CONFIG2                 0x6000E048
#define ANA_I2C_SAR_FORCE_PD        (1u << 18)      /* in ANA_CONFIG: cut the bus to the ADC */
#define ANA_I2C_SAR_FORCE_PU        (1u << 16)      /* in ANA_CONFIG2: connect it */
#define EFUSE_RD_SYS_PART1_DATA4    0x6000886C      /* eFuse block 2, bits 159:128 */
#define EFUSE_RD_SYS_PART1_DATA5    0x60008870      /* bits 191:160 */

#endif
