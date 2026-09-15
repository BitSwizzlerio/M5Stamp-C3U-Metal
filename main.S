/*
 * main.S - C3U-Metal: the RGB LED lights while the button is held.
 *
 * No ESP-IDF and no FreeRTOS. start.S has already switched off the
 * watchdogs, set the CPU to 40 MHz and set up a stack; everything here
 * talks to the hardware registers directly.
 *
 *   Button : GPIO9, reads 0 while pressed
 *   LED    : GPIO2, one SK6812 addressable LED (24 bits, G-R-B order)
 */
#include "esp32c3-regs.h"

#define LED_PIN           2
#define BTN_PIN           9
#define COLOR             0x002000              /* 0x00GGRRBB: dim red */

/* SK6812 bit timing in CPU cycles at 40 MHz (one cycle = 25 ns) */
#define MHZ               40
#define T0H               (MHZ * 400 / 1000)    /* "0" bit high: 400 ns = 16 cycles */
#define T1H               (MHZ * 800 / 1000)    /* "1" bit high: 800 ns = 32 cycles */
#define TBIT              (MHZ * 1250 / 1000)   /* every bit:   1250 ns = 50 cycles */

/*
 * The wait loops, and the instructions around them, take a few cycles longer
 * than the number they wait for. Measured on the board with a debugger (a3 and
 * a4 in send_grb) with no correction: high time 22 cycles instead of 16, bit
 * length 66 instead of 50. The waits are shortened by those amounts.
 * With these values the board measured 18 cycles high (450 ns) and 48 cycles
 * per bit (1200 ns); both include an instruction or two of the measuring code.
 */
#define HIGH_LATENCY      6
#define BIT_LATENCY       16

#define POLL_CYCLES       (MHZ * 1000000 / 50)  /* 20 ms */


        .section .text, "ax"
        .global main
main:
        /* LED pin (GPIO2): GPIO function, no input, no pulls */
        li   t0, IO_MUX_GPIO2
        lw   t1, 0(t0)
        li   t2, IO_MUX_CLEAR_MASK
        not  t2, t2
        and  t1, t1, t2
        li   t3, IO_MUX_MCU_SEL_GPIO
        or   t1, t1, t3
        sw   t1, 0(t0)

        li   t0, GPIO_FUNC2_OUT_SEL_CFG
        li   t1, SIG_GPIO_OUT_IDX           /* GPIO2 follows the GPIO_OUT register */
        sw   t1, 0(t0)
        li   t1, 1 << LED_PIN
        li   t0, GPIO_OUT_W1TC
        sw   t1, 0(t0)                      /* output low first ...        */
        li   t0, GPIO_ENABLE_W1TS
        sw   t1, 0(t0)                      /* ... then turn the driver on */

        /* Button pin (GPIO9): GPIO function, input enabled, pull-up on, no output */
        li   t0, IO_MUX_GPIO9
        lw   t1, 0(t0)
        li   t2, IO_MUX_CLEAR_MASK
        not  t2, t2
        and  t1, t1, t2
        li   t3, IO_MUX_MCU_SEL_GPIO | IO_MUX_FUN_IE | IO_MUX_FUN_PU
        or   t1, t1, t3
        sw   t1, 0(t0)
        li   t0, GPIO_ENABLE_W1TC
        li   t1, 1 << BTN_PIN
        sw   t1, 0(t0)

        li   s1, 0                          /* s1 = passes through the loop (for the debugger) */

loop:
        li   t0, GPIO_IN
        lw   t1, 0(t0)
        andi t1, t1, 1 << BTN_PIN           /* t1 = 0 while pressed */
        li   a0, 0                          /* released: LED off */
        bnez t1, 1f
        li   a0, COLOR                      /* pressed: LED on   */
1:      call send_grb
        call delay_poll
        addi s1, s1, 1
        j    loop


/*
 * send_grb(a0 = 0x00GGRRBB): send 24 bits to the LED, most significant first.
 * This program never enables interrupts, so nothing can stretch a pulse.
 *
 * For checking the timing with a debugger, two registers keep their last
 * values (nothing else in the program uses them):
 *   a3 = high time of the last bit, a4 = length of the last full bit, in cycles.
 */
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
        sub  a4, a1, t4                     /* debug: length of the previous bit */
        mv   t4, a1
2:      csrr a1, CSR_PCCR
        sub  a1, a1, t4
        bltu a1, t5, 2b                     /* wait out the high time */
        sw   t2, 0(t1)                      /* pin low */
        csrr a1, CSR_PCCR
        sub  a3, a1, t4                     /* debug: high time of this bit */
3:      csrr a1, CSR_PCCR
        sub  a1, a1, t4
        bltu a1, t3, 3b                     /* wait out the rest of the bit */

        slli a0, a0, 1                      /* next bit */
        addi a2, a2, -1
        bnez a2, next_bit
        ret


/* delay_poll: busy-wait 20 ms using the cycle counter */
delay_poll:
        csrr t0, CSR_PCCR
        li   t1, POLL_CYCLES
1:      csrr t2, CSR_PCCR
        sub  t2, t2, t0
        bltu t2, t1, 1b
        ret
