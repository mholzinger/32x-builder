/* Freestanding shim for the sh-elf build (marsdev ships no libc
 * headers): just what libspeex's os_support.h needs. Implementations
 * live in spx_glue.c. Found via -Ish_src/speex, so it shadows nothing
 * outside the speex objects. */
#ifndef SPX_SHIM_STRING_H
#define SPX_SHIM_STRING_H

#ifdef SPX_HOST_TEST
/* Host harness: defer to the real system header. */
#include_next <string.h>
#else


#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
#endif /* SPX_HOST_TEST */

#endif
