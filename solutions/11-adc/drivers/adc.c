/*
 * adc.c - read a voltage on GPIO0 to GPIO4 with the SAR ADC.
 * Solution to exercise 11.
 *
 * A SAR ("successive approximation register") ADC finds a voltage the way you
 * would guess a number between 0 and 4095 by asking "higher or lower?": it
 * compares the pin with half its range, then a quarter, and so on, 12 times,
 * one bit of the answer each time.
 *
 * Each pin goes through an attenuator first, which divides the voltage so that
 * a bigger range fits: 4095 is about 0.8 V with attenuation 0, and about 2.9 V
 * with attenuation 3. Anything above that reads 4095.
 *
 * No two chips' ADCs are exactly alike. Each reads a little above 0 at 0 V, by a
 * different amount for each attenuation. The factory measured that amount and
 * burned it into eFuse; this driver reads it and gives it to the ADC, which then
 * takes it off every reading. Without it, 0 V reads about 1400 to 1700.
 *
 * Read first: docs/exercises/11-adc.md, then drivers/gpio.c for the pad registers.
 */
#include <stdint.h>
#include "board.h"
#include "cpu.h"
#include "esp32c3-regs.h"
#include "adc.h"

/*
 * Some of the ADC's settings are not memory-mapped registers. They live on a small
 * bus inside the chip that reaches its analog blocks, and the ROM has a function
 * for writing them (ESP-IDF's regi2c_saradc.h and esp32c3.rom.ld).
 */
#define rom_analog_write    ((void (*)(uint8_t block, uint8_t host, uint8_t reg, \
                                       uint8_t msb, uint8_t lsb, uint8_t data))0x40001960)
#define SAR_ADC             0x69        /* the ADC's block number on that bus */
#define SAR_HOST            0
#define SAR1_CODE_LOW       0x0         /* bits 7:0: the low 8 bits of the zero offset */
#define SAR1_CODE_HIGH      0x1         /* bits 3:0: its top 4 bits */
#define SAR1_DREF           0x2         /* bits 6:4: the reference voltage setting */

#define START_CYCLES        (3 * CPU_CYCLES_PER_US)     /* hold START low for 3 us, so the ADC sees it change */
#define TIMEOUT_CYCLES      CPU_CYCLES_PER_MS           /* a conversion takes far less than 1 ms */

static void wait(uint32_t cycles)
{
    uint32_t start = cycle_count();

    while (cycle_count() - start < cycles) {
    }
}

void adc_init(void)
{
    /* Give the ADC's controller its clock, and reset it. */
    REG(SYSTEM_PERIP_CLK_EN0) |= SYSTEM_APB_SARADC_CLK_EN;
    REG(SYSTEM_PERIP_RST_EN0) |= SYSTEM_APB_SARADC_RST;
    REG(SYSTEM_PERIP_RST_EN0) &= ~SYSTEM_APB_SARADC_RST;

    /* The controller's clock: APB divided by 15 + 0/1 + 1, which is 2.5 MHz at 40 MHz. */
    REG(APB_SARADC_CLKM_CONF) = APB_SARADC_CLK_SEL_APB | APB_SARADC_CLK_EN
                              | (1u << APB_SARADC_CLKM_DIV_B_S) | (15u << APB_SARADC_CLKM_DIV_NUM_S);

    /* Power the ADC on (by us, not by the controller), and clock it at the controller's speed. */
    REG(APB_SARADC_CTRL) = (REG(APB_SARADC_CTRL) & ~APB_SARADC_CTRL_CLEAR)
                         | APB_SARADC_XPD_SAR_ON | (1u << APB_SARADC_SAR_CLK_DIV_S) | APB_SARADC_SAR_CLK_GATED;

    /* Open the analog bus to the ADC, and set the reference as ESP-IDF does. */
    REG(ANA_CONFIG) &= ~ANA_I2C_SAR_FORCE_PD;
    REG(ANA_CONFIG2) |= ANA_I2C_SAR_FORCE_PU;
    rom_analog_write(SAR_ADC, SAR_HOST, SAR1_DREF, 6, 4, 1);
}

/*
 * This chip's zero offset at attenuation 'atten', from eFuse block 2. Bits 1:0 of
 * the block say whether calibration was burned at all (1 means yes); the four
 * offsets follow from bit 20, 10 bits each, stored minus 1000. 0 if there are none.
 */
static uint32_t zero_offset(int atten)
{
    uint64_t block = ((uint64_t)REG(EFUSE_RD_SYS_PART1_DATA5) << 32) | REG(EFUSE_RD_SYS_PART1_DATA4);

    if ((block & 3) != 1)
        return 0;
    return (uint32_t)((block >> (20 + 10 * atten)) & 0x3FF) + 1000;
}

int adc_read(int pin, int atten)
{
    uint32_t offset = zero_offset(atten);
    uint32_t start, value;

    rom_analog_write(SAR_ADC, SAR_HOST, SAR1_CODE_HIGH, 3, 0, (uint8_t)(offset >> 8));
    rom_analog_write(SAR_ADC, SAR_HOST, SAR1_CODE_LOW, 7, 0, (uint8_t)offset);

    /* An analog pad: output off, and in IO_MUX the digital input and both pulls off. */
    REG(GPIO_ENABLE_W1TC) = 1u << pin;
    REG(IO_MUX_GPIO(pin)) = (REG(IO_MUX_GPIO(pin)) & ~IO_MUX_CLEAR_MASK) | IO_MUX_MCU_SEL_GPIO;

    /* One conversion on ADC1. Its channel numbers are the GPIO numbers. */
    REG(APB_SARADC_ONETIME_SAMPLE) = APB_SARADC_ADC1_ONETIME
                                   | ((uint32_t)pin << APB_SARADC_ONETIME_CHANNEL_S)
                                   | ((uint32_t)atten << APB_SARADC_ONETIME_ATTEN_S);
    REG(APB_SARADC_INT_CLR) = APB_SARADC_ADC1_DONE;
    wait(START_CYCLES);
    REG(APB_SARADC_ONETIME_SAMPLE) |= APB_SARADC_ONETIME_START;

    start = cycle_count();
    while (!(REG(APB_SARADC_INT_RAW) & APB_SARADC_ADC1_DONE))
        if (cycle_count() - start > TIMEOUT_CYCLES)
            return ADC_FAILED;
    value = REG(APB_SARADC_1_DATA_STATUS) & 0xFFF;

    REG(APB_SARADC_ONETIME_SAMPLE) = 0;                 /* START low again, ready for the next one */
    return (int)value;
}
