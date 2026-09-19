/*
 * sk6812.S - sk6812_send_grb(): send a colour to the SK6812 RGB LED on LED_PIN.
 *
 * Called from C as:  void sk6812_send_grb(uint32_t grb);   (declared in sk6812.h)
 *
 * It stays in assembly because its pulses are timed to a few CPU cycles, and a
 * C compiler is free to change how many instructions a loop takes.
 *
 * Read first: drivers/gpio.c, and chip/cpu.S for the cycle counter.
 * Try this:   python tools/read_registers.py sk6812_last_high sk6812_last_bit
 *             shows the last bit's timing in cycles (about 17 and 51). Set
 *             HIGH_LATENCY to 0, flash, and measure again. Exercise 8 does this
 *             job with the chip's RMT peripheral instead.
 */
#include "board.h"
#include "esp32c3-regs.h"

#if CPU_MHZ != 160
#error "sk6812.S: HIGH_LATENCY and BIT_LATENCY were measured at 160 MHz and must be measured again"
#endif

/* SK6812 bit timing in CPU cycles (one cycle = 6.25 ns at 160 MHz) */
#define T0H               (CPU_MHZ * 400 / 1000)    /* "0" bit high: 400 ns = 64 cycles */
#define T1H               (CPU_MHZ * 800 / 1000)    /* "1" bit high: 800 ns = 128 cycles */
#define TBIT              (CPU_MHZ * 1250 / 1000)   /* every bit:   1250 ns = 200 cycles */
#define TRESET            (CPU_MHZ * 100)           /* low between colours: 100 us; the LED latches after 80 us */

/*
 * The wait loops, and the instructions around them, take a few cycles longer
 * than the number they wait for. Measured on the board with a debugger with no
 * correction: high time 22 cycles instead of 16, bit length 66 instead of 50.
 * The waits are shortened by those amounts. With these values the first,
 * all-assembly version (lesson step 1) measured 18 cycles high and 48 per bit;
 * this driver measures 17 cycles high (425 ns) and 51 per bit (1275 ns). Both
 * include an instruction or two of the measuring code.
 *
 * At 160 MHz (solution to exercise 2) the same corrections still fit, because
 * an instruction takes the same number of cycles whatever the clock speed.
 * The board measured 66 cycles high (412 ns) and 200 cycles per bit (1250 ns).
 */
#define HIGH_LATENCY      6
#define BIT_LATENCY       16


/*
 * sk6812_send_grb(a0 = 0x00GGRRBB): send 24 bits to the LED, most significant first.
 * No interrupt source is connected to the CPU in this program, so nothing can
 * stretch a pulse. (Exercise 4 adds interrupts, and turns them off in here.)
 * Uses only a0-a4 and t0-t5, which the calling convention lets it overwrite.
 *
 * a3 and a4 measure each bit as it is sent. The last values are kept in
 * sk6812_last_high and sk6812_last_bit, for checking the timing with a
 * debugger. Those instructions are part of the loop that HIGH_LATENCY and
 * BIT_LATENCY were measured with, so taking them out would change the timing.
 *
 * Placed in .iram1, which c3u-metal.ld runs from RAM: code in flash can stall
 * while the flash cache loads it, and a stall in the middle of a pulse would
 * corrupt the colour.
 */
        .section .iram1, "ax"
        .global sk6812_send_grb
sk6812_send_grb:
        /*
         * The LED only takes in a colour once its data line has been low for
         * 80 us. Calls can come faster than that (a Lua loop manages one every
         * 55 us), so first wait until the previous colour is TRESET cycles old.
         */
        la   t0, sk6812_last_end
        lw   t1, 0(t0)
        li   t2, TRESET
9:      csrr t3, CSR_PCCR
        sub  t3, t3, t1
        bltu t3, t2, 9b

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
        la   t0, sk6812_last_high
        sw   a3, 0(t0)
        la   t0, sk6812_last_bit
        sw   a4, 0(t0)
        csrr t1, CSR_PCCR
        la   t0, sk6812_last_end
        sw   t1, 0(t0)                      /* when this colour ended, for the wait at the top */
        ret


        .section .bss
        .align 2
        .global sk6812_last_high
        .global sk6812_last_bit
sk6812_last_high:  .skip 4                  /* high time of the last bit sent, in cycles */
sk6812_last_bit:   .skip 4                  /* length of the last full bit sent, in cycles */
sk6812_last_end:   .skip 4                  /* cycle count when the last colour finished */
