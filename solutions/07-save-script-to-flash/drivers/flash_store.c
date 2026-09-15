/*
 * flash_store.c - keep one piece of text in the last 4 KB sector of flash.
 * Part of the solution to exercise 7.
 *
 * The program sits at the start of the 4 MB flash. The store is the last
 * sector, at offset 0x3FF000, laid out like this:
 *
 *   offset 0   STORE_MAGIC, so that an erased sector (all 0xFF) reads as empty
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
#define STORE_OFFSET        0x3FF000u                       /* the last sector of a 4 MB flash */
#define STORE_MAPPED        (0x3C000000u + STORE_OFFSET)    /* the same bytes, read through the cache */
#define STORE_MAGIC         0x3141554Cu                     /* the bytes "LUA1" */

/* ROM functions, at the addresses listed in ESP-IDF's components/esp_rom/esp32c3/ld/esp32c3.rom.ld */
#define rom_spiflash_config_param   ((int (*)(uint32_t id, uint32_t chip_size, uint32_t block_size, \
                                              uint32_t sector_size, uint32_t page_size, uint32_t status_mask))0x40000134)
#define rom_spiflash_erase_sector   ((int (*)(uint32_t sector))0x40000128)
#define rom_spiflash_write          ((int (*)(uint32_t offset, const uint32_t *data, uint32_t len))0x4000012c)
#define rom_cache_suspend_icache    ((uint32_t (*)(void))0x40000524)
#define rom_cache_resume_icache     ((void (*)(uint32_t autoload))0x40000528)
#define rom_cache_invalidate_addr   ((void (*)(uint32_t address, uint32_t size))0x400004d4)

static uint32_t sector[SECTOR_SIZE / 4];        /* what to write, in RAM; whole words, as the ROM wants */

/* Erase the store's sector, then write the first 'nbytes' of 'sector' (a multiple of 4). */
__attribute__((section(".iram1"), noinline))
static int write_sector(uint32_t nbytes)
{
    uint32_t autoload = rom_cache_suspend_icache();         /* from here on, no reading from flash */
    int err = rom_spiflash_erase_sector(STORE_OFFSET / SECTOR_SIZE);

    if (err == 0 && nbytes > 0)
        err = rom_spiflash_write(STORE_OFFSET, sector, nbytes);
    rom_cache_resume_icache(autoload);
    return err;
}

/* Erase and write, then make sure reads through the cache see the new contents. */
static bool update(uint32_t nbytes)
{
    static bool configured;

    if (!configured) {
        /* In direct boot nothing has told the ROM's flash driver the chip's size yet. */
        rom_spiflash_config_param(0, 4u * 1024 * 1024, 64u * 1024, SECTOR_SIZE, 256, 0xFFFF);
        configured = true;
    }
    int err = write_sector(nbytes);
    rom_cache_invalidate_addr(STORE_MAPPED, SECTOR_SIZE);
    return err == 0;
}

bool flash_store_save(const char *text, size_t len)
{
    if (len > FLASH_STORE_MAX)
        return false;
    memset(sector, 0xFF, sizeof sector);
    sector[0] = STORE_MAGIC;
    sector[1] = (uint32_t)len;
    memcpy(&sector[2], text, len);
    return update((uint32_t)(8 + len + 3) & ~3u);           /* round up to whole words */
}

bool flash_store_erase(void)
{
    return update(0);
}

const char *flash_store_load(size_t *len)
{
    const uint32_t *store = (const uint32_t *)STORE_MAPPED;

    if (store[0] != STORE_MAGIC || store[1] > FLASH_STORE_MAX)
        return NULL;
    *len = store[1];
    return (const char *)&store[2];
}
