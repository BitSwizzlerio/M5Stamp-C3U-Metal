/*
 * string.c - the four memory functions GCC may call on its own.
 *
 * Even with no C library, the compiler can turn struct copies and large
 * zero-initialisations into calls to these, so a bare-metal runtime has to
 * provide them. build.ps1 compiles with -fno-tree-loop-distribute-patterns so
 * the optimiser can't turn the loop inside memset() into a call to memset().
 */
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {                        /* overlapping with dest after src: copy backwards */
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = dest;

    while (n--)
        *d++ = (unsigned char)c;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a;
    const unsigned char *q = b;

    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}
