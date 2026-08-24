/* Speex glue for the 32X: attachable bump-arena allocator + diagnostic
 * stubs + (on the SH-2 only) the string.h primitives the -nostdlib link
 * lacks.
 *
 * The decoder allocates exactly twice, both at init: the 1,912 B
 * NBDecState and its NB_DEC_STACK scratch block (see config.h). The
 * arena is NOT static storage — sound.c attaches a slice of box_hero's
 * hero_scratch overlay (title-only, dead all game long) and re-attaches
 * on every ambient-audio activation, so a title replay clobbering the
 * region is healed before the next decode. Nothing allocates after
 * attach+init.
 */
#include "config.h"
#include <stdint.h>
#include <stddef.h>

static uint8_t *spx_arena = 0;
static unsigned spx_arena_size = 0;
unsigned spx_arena_used = 0;    /* high-water mark, HUD-readable */
unsigned spx_warn_count = 0;    /* bumped by decoder warnings, HUD-readable */

/* (Re)point the arena at a memory slice and reset the bump pointer.
 * Callers own the phase-disjointness argument for the slice. */
void spx_arena_attach(void *base, unsigned size) {
    spx_arena      = (uint8_t *)base;
    spx_arena_size = size;
    spx_arena_used = 0;
}

static void *arena_take(int size) {
    if (size <= 0 || !spx_arena) return 0;
    unsigned aligned = (spx_arena_used + 7u) & ~7u;
    if (aligned + (unsigned)size > spx_arena_size) {
        spx_warn_count += 1000;   /* unmistakable HUD signature */
        return 0;
    }
    uint8_t *p = spx_arena + aligned;
    for (int i = 0; i < size; i++) p[i] = 0;   /* calloc semantics */
    spx_arena_used = aligned + (unsigned)size;
    return p;
}

void *speex_alloc(int size)         { return arena_take(size); }
void *speex_alloc_scratch(int size) { return arena_take(size); }
void *speex_realloc(void *ptr, int size) { (void)ptr; (void)size;
                                           spx_warn_count += 1000; return 0; }
void speex_free(void *ptr)          { (void)ptr; }
void speex_free_scratch(void *ptr)  { (void)ptr; }

#ifdef SPX_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
void _speex_fatal(const char *str, const char *file, int line) {
    fprintf(stderr, "SPEEX FATAL: %s (%s:%d)\n", str, file, line);
    abort();
}
void speex_warning(const char *str) {
    fprintf(stderr, "speex warning: %s\n", str); spx_warn_count++;
}
void speex_warning_int(const char *str, int val) {
    fprintf(stderr, "speex warning: %s %d\n", str, val); spx_warn_count++;
}
#else
void _speex_fatal(const char *str, const char *file, int line) {
    (void)str; (void)file; (void)line;
    spx_warn_count += 10000;
    for (;;) {}   /* fatal is a real bug — park so the HUD count survives */
}
void speex_warning(const char *str)              { (void)str; spx_warn_count++; }
void speex_warning_int(const char *str, int val) { (void)str; (void)val; spx_warn_count++; }
#endif
void speex_notify(const char *str)               { (void)str; }
void _speex_putc(int ch, void *file)             { (void)ch; (void)file; }

#ifndef SPX_HOST_TEST
/* string.h primitives for the -nostdlib SH-2 link. Nothing else in the
 * ROM defines these; Speex (and any gcc-emitted builtin calls) resolve
 * here. Byte loops — these run at init and per 20 ms frame on buffers
 * of a few hundred bytes, nowhere near hot. */
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
#endif

#ifdef SPX_HOST_TEST
/* Host harness: a static arena stands in for the hero_scratch slice,
 * self-attached on first use so init order doesn't matter. Also lets
 * the scratch probe locate the arena base. Never compiled for SH-2. */
static uint8_t host_arena[262144];
__attribute__((constructor)) static void host_attach(void) {
    spx_arena_attach(host_arena, sizeof host_arena);
}
unsigned char *spx_arena_base(void) { return spx_arena; }
#endif
