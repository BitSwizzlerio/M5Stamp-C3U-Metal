/*
 * c3u-metal.ld - memory layout for a bare-metal ESP32-C3 program using direct boot.
 *
 * In direct boot the ROM checks that flash starts with a magic number, maps the
 * first 4 MB of flash to 0x42000000 and jumps to 0x42000008. There is no
 * bootloader, so everything else in memory is up to this file and start.S.
 */
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    irom (rx) : ORIGIN = 0x42000000, LENGTH = 4M       /* flash, mapped by the ROM for running code   */
    ram  (rw) : ORIGIN = 0x3FC80000, LENGTH = 0x50000  /* SRAM for us; the ROM uses the area above it */
}

SECTIONS
{
    /* Flash offset 0: the ROM only runs our code if it finds this value twice. */
    .header : AT(0)
    {
        LONG(0xAEDB041D)
        LONG(0xAEDB041D)
    } > irom

    /* The ROM jumps to 0x42000008, so _start (section .text.entry) must be exactly here. */
    .text.entry ORIGIN(irom) + 8 :
    {
        KEEP(*(.text.entry))
    } > irom

    /* All other code follows. */
    .text :
    {
        *(.text .text.*)
    } > irom

    /*
     * Not supported yet: read-only data, initialised data and zeroed data.
     * Each needs more work (a second flash mapping, or startup code that copies
     * or clears RAM), so the link fails if any of them are used.
     */
    .unsupported (NOLOAD) :
    {
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        *(.bss .bss.* .sbss .sbss.* COMMON)
    } > ram
    ASSERT(SIZEOF(.unsupported) == 0, "c3u-metal.ld: .rodata, .data and .bss are not supported yet")

    /* The stack starts at the top of our RAM area and grows down. */
    _stack_top = ORIGIN(ram) + LENGTH(ram);
}
