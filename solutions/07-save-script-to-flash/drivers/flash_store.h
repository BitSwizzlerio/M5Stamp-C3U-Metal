/*
 * flash_store.h - keep one piece of text, such as a Lua script, in the last sector of flash.
 * Part of the solution to exercise 7.
 */
#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdbool.h>
#include <stddef.h>

#define FLASH_STORE_MAX     4088            /* the most text that fits: a 4 KB sector minus the header */

bool        flash_store_save(const char *text, size_t len);    /* replace what is stored; false on error */
bool        flash_store_erase(void);                            /* remove it; false on error */
const char *flash_store_load(size_t *len);                      /* what is stored (read from flash), or NULL */

#endif
