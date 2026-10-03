/*
 * adc.h - read a voltage on GPIO0 to GPIO4 with the SAR ADC.
 * Solution to exercise 11.
 */
#ifndef ADC_H
#define ADC_H

#define ADC_MAX_PIN     4               /* ADC1's channels 0-4 are GPIO0-GPIO4 */
#define ADC_FAILED      (-1)

void adc_init(void);

/* A 12-bit reading, 0-4095, of 'pin' with attenuation 0-3, or ADC_FAILED. The pin
   becomes an analog input: no digital input, no output, no pull resistors. */
int adc_read(int pin, int atten);

#endif
