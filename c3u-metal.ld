/*
 * c3u-metal.ld - memory layout for a bare-metal ESP32-C3 program using direct boot.
 *
 * In direct boot the ROM checks that flash starts with a magic number, maps the
 * first 4 MB of flash to 0x42000000 (for code) and 0x3C000000 (for data), and
 * jumps to 0x42000008. There is no bootloader, so everything else in memory is
 * up to this file and crt0.S.
 */
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    irom (rx) : ORIGIN = 0x42000000, LENGTH = 4M       /* flash, mapped for running code           */
    drom (r)  : ORIGIN = 0x3C000000, LENGTH = 4M       /* the same flash, mapped for reading data  */
    ram  (rw) : ORIGIN = 0x3FC80000, LENGTH = 0x50000  /* SRAM for us; the ROM uses the area above */
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
        . = ALIGN(4);
    } > irom

    /* How much flash the code uses: also the flash offset where .rodata starts. */
    _flash_used = . - ORIGIN(irom);

    /*
     * .rodata: constants and strings. The CPU reads data through the DROM mapping,
     * so the address is DROM + the flash offset, and AT() stores the bytes at that
     * same flash offset in the image.
     */
    .rodata ORIGIN(drom) + _flash_used : AT(_flash_used)
    {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > drom

    /*
     * .data: variables with a starting value. They live in RAM, but their starting
     * values are stored in flash right after .rodata; crt0.S copies them across.
     */
    .data ORIGIN(ram) : AT(LOADADDR(.rodata) + SIZEOF(.rodata))
    {
        _data_start = .;
        *(.data .data.*)
        __SDATA_BEGIN__ = .;
        *(.sdata .sdata.*)                  /* small variables, reachable through gp */
        . = ALIGN(4);
        _data_end = .;
    } > ram
    _data_lma = ORIGIN(drom) + LOADADDR(.data);     /* where crt0.S reads the starting values */

    /* .bss: variables that start at zero. Nothing is stored in flash; crt0.S clears it. */
    .bss (NOLOAD) :
    {
        _bss_start = .;
        *(.sbss .sbss.*)                    /* small ones first, near gp */
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    /*
     * gp points into the small-variable area so GCC can reach variables within
     * 2 KB of it in one instruction. This is the formula GNU ld uses by default.
     */
    __global_pointer$ = MIN(__SDATA_BEGIN__ + 0x800, MAX(_data_start + 0x800, _bss_end - 0x800));

    /* The stack starts at the top of our RAM area and grows down. */
    _stack_top = ORIGIN(ram) + LENGTH(ram);
    ASSERT(_bss_end + 0x4000 <= _stack_top, "c3u-metal.ld: less than 16 KB left for the stack")

    /* Unwinding tables are only for C++ exceptions and debuggers that don't use DWARF. */
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}
