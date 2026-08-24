/* Speex build configuration for the 32X secondary SH-2 (and the host
 * sanity harness — both build with -DHAVE_CONFIG_H).
 *
 * Narrowband fixed-point DECODE is the only path we exercise; the
 * encoder objects are compiled (modes.c references them) but never
 * called from ROM code. No heap: all speex_alloc traffic lands in the
 * static arena in spx_glue.c. */
#ifndef SPX_CONFIG_H
#define SPX_CONFIG_H

#define FIXED_POINT        1
#define DISABLE_WIDEBAND   1
#define DISABLE_ENCODER    1
#define DISABLE_VBR        1
#define DISABLE_FLOAT_API  1
#define RELEASE            1   /* strips os_support.h's printf debug helper */

/* Decoder scratch (pseudo-stack) size. Upstream defaults to 16,000 B —
 * a one-size-fits-all guess. Pattern-paint measurement over the entire
 * baked Voyager bitstream (13,259 frames, enhancer on) shows a true
 * high-water of 1,124 B. 4 KB is ~3.6x that: the arena lives in the
 * hero_scratch overlay slice, not .bss, so margin is free. If the
 * asset is ever re-baked at a different quality, re-run the host
 * scratch probe (see DEVLOG). */
#define NB_DEC_STACK 4096
#define EXPORT             /* no symbol visibility annotations */

/* Route all allocation into the static bump arena (spx_glue.c). The
 * decoder allocates only at init (state + NB_DEC_STACK scratch); there
 * is no steady-state allocation, so free/realloc are stubs. */
#define OVERRIDE_SPEEX_ALLOC          1
#define OVERRIDE_SPEEX_ALLOC_SCRATCH  1
#define OVERRIDE_SPEEX_REALLOC        1
#define OVERRIDE_SPEEX_FREE           1
#define OVERRIDE_SPEEX_FREE_SCRATCH   1
#define OVERRIDE_SPEEX_FATAL          1
#define OVERRIDE_SPEEX_WARNING        1
#define OVERRIDE_SPEEX_WARNING_INT    1
#define OVERRIDE_SPEEX_NOTIFY         1
#define OVERRIDE_SPEEX_PUTC           1

void spx_arena_attach(void *base, unsigned size);
void *speex_alloc(int size);
void *speex_alloc_scratch(int size);
void *speex_realloc(void *ptr, int size);
void speex_free(void *ptr);
void speex_free_scratch(void *ptr);
void _speex_fatal(const char *str, const char *file, int line);
void speex_warning(const char *str);
void speex_warning_int(const char *str, int val);
void speex_notify(const char *str);
void _speex_putc(int ch, void *file);

#endif
