/*
 * board.h - the M5Stamp C3U board, and the speed this program runs its chip at.
 *
 * Included by both C and assembly files, so it holds plain #defines only.
 */
#ifndef BOARD_H
#define BOARD_H

/*
 * CPU speed. SystemInit() (chip/system_esp32c3.c) runs the CPU straight from
 * the board's 40 MHz crystal, so the cycle counter counts CPU_MHZ million
 * cycles a second. Delays, uptime and the LED's pulses are all timed with it.
 * Files that only work at 40 MHz stop the build with #error if this changes.
 */
#define CPU_MHZ             40
#define CPU_CYCLES_PER_MS   (CPU_MHZ * 1000)
#define CPU_CYCLES_PER_US   CPU_MHZ

/* Pins */
#define LED_PIN             2       /* data line of the SK6812 RGB LED */
#define BTN_PIN             9       /* the button pulls the pin to ground: reads 0 while pressed.
                                       Held while plugging in, it starts the chip's download mode. */

#endif
