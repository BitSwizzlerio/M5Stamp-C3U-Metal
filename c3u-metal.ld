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
    irom (rx)  : ORIGIN = 0x42000000, LENGTH = 4M       /* flash, mapped for running code           */
    drom (r)   : ORIGIN = 0x3C000000, LENGTH = 4M       /* the same flash, mapped for reading data  */
    ram  (rw)  : ORIGIN = 0x3FC80000, LENGTH = 0x50000  /* SRAM for us; the ROM uses the area above */
    iram (rwx) : ORIGIN = 0x40380000, LENGTH = 0x50000  /* the same SRAM, at the address it can run code from */
}

STACK_SIZE = 0x10000;                                   /* 64 KB: Lua's parser recurses on the C stack */
IRAM_TO_DRAM = 0x40380000 - 0x3FC80000;                 /* 0x700000: same RAM byte, code address minus data address */

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

    /* All other code that runs straight from flash. */
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
     * same flash offset in the image. The sections after it follow the same way.
     */
    .rodata ORIGIN(drom) + _flash_used : AT(_flash_used)
    {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > drom

    /* Lists of functions newlib's __libc_init_array() runs before main (e.g. C++ constructors). */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > drom
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > drom
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > drom

    /* Flash offset just past all the read-only data. */
    _drom_used = . - ORIGIN(drom);

    /*
     * .iram.text: code that must never wait on the flash cache (send_grb's timed
     * pulses). It runs from RAM at the start of our area, is stored in flash after
     * the read-only data, and crt0.S copies it into RAM before main().
     */
    .iram.text ORIGIN(iram) : AT(_drom_used)
    {
        _iram_text_start = .;
        *(.iram1 .iram1.*)
        . = ALIGN(4);
        _iram_text_end = .;
    } > iram
    _iram_text_lma  = ORIGIN(drom) + LOADADDR(.iram.text);     /* crt0.S reads the code from here ...   */
    _iram_text_dram = _iram_text_start - IRAM_TO_DRAM;          /* ... and writes it to the same RAM here */

    /*
     * .data: variables with a starting value. They live in RAM just after the
     * RAM code, and their starting values are stored in flash after it too;
     * crt0.S copies them across.
     */
    .data ORIGIN(ram) + SIZEOF(.iram.text) : AT(LOADADDR(.iram.text) + SIZEOF(.iram.text))
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

    /* Stack: the top STACK_SIZE bytes of our RAM, growing down. */
    _stack_top    = ORIGIN(ram) + LENGTH(ram);
    _stack_bottom = _stack_top - STACK_SIZE;

    /* Heap: everything between the variables and the stack, handed out by _sbrk() in syscalls.c. */
    _heap_start = ALIGN(_bss_end, 8);
    _heap_end   = _stack_bottom;
    ASSERT(_heap_end >= _heap_start + 0x20000, "c3u-metal.ld: less than 128 KB left for the heap")

    /* Unwinding tables are only for C++ exceptions and debuggers that don't use DWARF. */
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}
