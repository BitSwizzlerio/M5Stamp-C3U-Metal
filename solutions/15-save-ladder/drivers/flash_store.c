/*
 * flash_store.c - keep pieces of text in the last 4 KB sectors of flash.
 * Part of the solutions to exercises 7 and 15.
 *
 * The program sits at the start of the 4 MB flash. Each slot is a sector of
 * its own: the Lua script (exercise 7) in the last one, at offset 0x3FF000,
 * and the ladder (exercise 15) in the one before, at 0x3FE000. Each is laid
 * out like this:
 *
 *   offset 0   the slot's magic number, so that an erased sector (all 0xFF)
 *              reads as empty, and one slot's text is never taken for another's
 *   offset 4   the length of the text
 *   offset 8   the text
 *
 * Erasing and writing use the flash functions in the chip's ROM. While they
 * run, the CPU can't read flash through the cache (the 0x42000000 and
 * 0x3C000000 addresses), so the function that calls them runs from RAM
 * (section .iram1, like sk6812_send_grb), and so does everything it touches:
 * its buffer is in .bss, and the ROM functions are called by address.
 */
#include <stdint.h>
#include <string.h>
#include "flash_store.h"

#define SECTOR_SIZE         4096u
#define MAPPED(offset)      (0x3C000000u + (offset))        /* the same bytes, read through the cache */

static const struct {
    uint32_t offset;
    uint32_t magic;
} slots[] = {
    [FLASH_SLOT_SCRIPT] = { 0x3FF000u, 0x3141554Cu },       /* the last sector of a 4 MB flash; "LUA1" */
    [FLASH_SLOT_LADDER] = { 0x3FE000u, 0x3152444Cu },       /* the one before; "LDR1" */
};

/* ROM functions, at the addresses listed in ESP-IDF's components/esp_rom/esp32c3/ld/esp32c3.rom.ld */
#define rom_spiflash_config_param   ((int (*)(uint32_t id, uint32_t chip_size, uint32_t block_size, \
                                              uint32_t sector_size, uint32_t page_size, uint32_t status_mask))0x40000134)
#define rom_spiflash_erase_sector   ((int (*)(uint32_t sector))0x40000128)
#define rom_spiflash_write          ((int (*)(uint32_t offset, const uint32_t *data, uint32_t len))0x4000012c)
#define rom_cache_suspend_icache    ((uint32_t (*)(void))0x40000524)
#define rom_cache_resume_icache     ((void (*)(uint32_t autoload))0x40000528)
#define rom_cache_invalidate_addr   ((void (*)(uint32_t address, uint32_t size))0x400004d4)

static uint32_t sector[SECTOR_SIZE / 4];        /* what to write, in RAM; whole words, as the ROM wants */

/* Erase the sector at 'offset', then write the first 'nbytes' of 'sector' (a multiple of 4). */
__attribute__((section(".iram1"), noinline))
static int write_sector(uint32_t offset, uint32_t nbytes)
{
    uint32_t mstatus;

    /* Interrupts off while the cache is off: the vector table and the handlers are
       in flash (that matters when exercise 4 is in the same build). The CSR is
       written directly here, so this needs nothing from that exercise. */
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));
    uint32_t autoload = rom_cache_suspend_icache();         /* from here on, no reading from flash */
    int err = rom_spiflash_erase_sector(offset / SECTOR_SIZE);

    if (err == 0 && nbytes > 0)
        err = rom_spiflash_write(offset, sector, nbytes);
    rom_cache_resume_icache(autoload);
    __asm__ volatile ("csrs mstatus, %0" : : "r"(mstatus & 8));    /* interrupts back on, if they were on */
    return err;
}

/* Erase and write, then make sure reads through the cache see the new contents. */
static bool update(enum flash_slot slot, uint32_t nbytes)
{
    static bool configured;

    if (!configured) {
        /* In direct boot nothing has told the ROM's flash driver the chip's size yet. */
        rom_spiflash_config_param(0, 4u * 1024 * 1024, 64u * 1024, SECTOR_SIZE, 256, 0xFFFF);
        configured = true;
    }
    int err = write_sector(slots[slot].offset, nbytes);
    rom_cache_invalidate_addr(MAPPED(slots[slot].offset), SECTOR_SIZE);
    return err == 0;
}

bool flash_store_save(enum flash_slot slot, const char *text, size_t len)
{
    if (len > FLASH_STORE_MAX)
        return false;
    memset(sector, 0xFF, sizeof sector);
    sector[0] = slots[slot].magic;
    sector[1] = (uint32_t)len;
    memcpy(&sector[2], text, len);
    return update(slot, (uint32_t)(8 + len + 3) & ~3u);     /* round up to whole words */
}

bool flash_store_erase(enum flash_slot slot)
{
    return update(slot, 0);
}

const char *flash_store_load(enum flash_slot slot, size_t *len)
{
    const uint32_t *store = (const uint32_t *)MAPPED(slots[slot].offset);

    if (store[0] != slots[slot].magic || store[1] > FLASH_STORE_MAX)
        return NULL;
    *len = store[1];
    return (const char *)&store[2];
}
