/* string.h primitives for the -nostdlib link. gcc emits calls to these
 * behind your back (struct copies, array zeroing, loop idioms
 * recognized at -O2/LTO) and neither -nostdlib nor -lgcc provides
 * them. Formerly lived in sh_src/speex/spx_glue.c; promoted here when
 * the speex objects were unlinked and the rest of the ROM turned out
 * to be leaning on them.
 *
 * MUST be compiled -fno-lto (a real object: LTO discards "unused" IR
 * definitions before it emits the late builtin calls that need them)
 * and -fno-builtin (else gcc rewrites these loops into calls to the
 * very functions they implement). See the Makefile rule.
 *
 * Byte loops on purpose: every current caller moves at most a few
 * hundred bytes outside hot paths. If a profile ever blames these,
 * word-align the fast path then. */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
