/*
 * led.S - send_grb(): drive the SK6812 addressable LED on GPIO2.
 *
 * Called from C as:  void send_grb(uint32_t grb);
 *
 * It stays in assembly because its pulses are timed to a few CPU cycles, and a
 * C compiler is free to change how many instructions a loop takes.
 */
#include "esp32c3-regs.h"

#define LED_PIN           2

/* SK6812 bit timing in CPU cycles at 40 MHz (one cycle = 25 ns) */
#define MHZ               40
#define T0H               (MHZ * 400 / 1000)    /* "0" bit high: 400 ns = 16 cycles */
#define T1H               (MHZ * 800 / 1000)    /* "1" bit high: 800 ns = 32 cycles */
#define TBIT              (MHZ * 1250 / 1000)   /* every bit:   1250 ns = 50 cycles */

/*
 * The wait loops, and the instructions around them, take a few cycles longer
 * than the number they wait for. Measured on the board with a debugger with no
 * correction: high time 22 cycles instead of 16, bit length 66 instead of 50.
 * The waits are shortened by those amounts. With these values the board
 * measured 18 cycles high (450 ns) and 48 cycles per bit (1200 ns); both include
 * an instruction or two of the measuring code.
 */
#define HIGH_LATENCY      6
#define BIT_LATENCY       16


/*
 * send_grb(a0 = 0x00GGRRBB): send 24 bits to the LED, most significant first.
 * This program never enables interrupts, so nothing can stretch a pulse.
 * Uses only a0-a4 and t0-t5, which the calling convention lets it overwrite.
 *
 * Placed in .iram1, which c3u-metal.ld runs from RAM: code in flash can stall
 * while the flash cache loads it, and a stall in the middle of a pulse would
 * corrupt the colour.
 */
        .section .iram1, "ax"
        .global send_grb
send_grb:
        li   t0, GPIO_OUT_W1TS
        li   t1, GPIO_OUT_W1TC
        li   t2, 1 << LED_PIN
        li   t3, TBIT - BIT_LATENCY
        li   a2, 24                         /* bits left */
        slli a0, a0, 8                      /* line bit 23 up with the sign bit */
        csrr t4, CSR_PCCR

next_bit:
        li   t5, T0H - HIGH_LATENCY         /* high time for a 0 ... */
        bgez a0, 1f
        li   t5, T1H - HIGH_LATENCY         /* ... or for a 1 */
1:      csrr a1, CSR_PCCR                   /* a1 = cycle count as this bit starts */
        sw   t2, 0(t0)                      /* pin high */
        sub  a4, a1, t4                     /* a4 = length of the previous bit */
        mv   t4, a1
2:      csrr a1, CSR_PCCR
        sub  a1, a1, t4
        bltu a1, t5, 2b                     /* wait out the high time */
        sw   t2, 0(t1)                      /* pin low */
        csrr a1, CSR_PCCR
        sub  a3, a1, t4                     /* a3 = high time of this bit */
3:      csrr a1, CSR_PCCR
        sub  a1, a1, t4
        bltu a1, t3, 3b                     /* wait out the rest of the bit */

        slli a0, a0, 1                      /* next bit */
        addi a2, a2, -1
        bnez a2, next_bit

        /* After the timed part: keep the last bit's measurements for a debugger */
        la   t0, led_last_high
        sw   a3, 0(t0)
        la   t0, led_last_bit
        sw   a4, 0(t0)
        ret


        .section .bss
        .align 2
        .global led_last_high
        .global led_last_bit
led_last_high:  .skip 4                     /* high time of the last bit sent, in cycles */
led_last_bit:   .skip 4                     /* length of the last full bit sent, in cycles */
