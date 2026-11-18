/*
 * flash_store.h - keep pieces of text, such as a Lua script, in the last sectors of flash.
 * Part of the solutions to exercises 7 and 15: exercise 15 adds a second slot, for the ladder.
 */
#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdbool.h>
#include <stddef.h>

#define FLASH_STORE_MAX     4088            /* the most text that fits: a 4 KB sector minus the header */

/* Each slot is one sector of its own. */
enum flash_slot {
    FLASH_SLOT_SCRIPT,                      /* save(): the Lua script, in the last sector (exercise 7) */
    FLASH_SLOT_LADDER,                      /* plc.save(): the ladder, in the sector before it (exercise 15) */
};

bool        flash_store_save(enum flash_slot slot, const char *text, size_t len);  /* false on error */
bool        flash_store_erase(enum flash_slot slot);                               /* false on error */
const char *flash_store_load(enum flash_slot slot, size_t *len);   /* what is stored (read from flash), or NULL */

#endif
