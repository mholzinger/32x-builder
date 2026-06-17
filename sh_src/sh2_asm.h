#ifndef SH2_ASM_H
#define SH2_ASM_H

#include <stdint.h>

/* SH-2 hardware division unit (DIVU) at 0xFFFFFF00. The SH-2 has no
 * divide instruction; integer division falls back to a software
 * routine in libgcc. The DIVU peripheral does 32×32→32 unsigned
 * division in 39 cycles, including the register-write protocol:
 *
 *   write divisor to base + 0
 *   write dividend hi to base + 16
 *   write dividend lo to base + 20 → starts the divide
 *   read quotient from base + 20 (CPU stalls if not yet done)
 *
 * Two flavors:
 *
 * 1. divu_start_u32 / divu_read — latency-hidden form. Start the
 *    divide, do other work for ~39 cycles, then read. The compiler
 *    won't reorder either side past the asm volatile block, but it
 *    can freely move unrelated C code between them.
 *
 * 2. divu_u32 — blocking form, for when there's no useful work to
 *    interleave. Saves ~10-50 cycles vs the GCC software divide
 *    even without latency hiding, because the hardware divide is
 *    ~5× faster than the software emulation.
 *
 * Adapted from viciious/d32xr (r_phase6.c:190-247).
 *
 * NOTE: both SH-2 CPUs have their OWN DIVU at the same address (it's
 * an on-chip peripheral). Primary and secondary can use this concurrently
 * without contention. */

/* Blocking divide — single asm block so start + read can't be
 * reordered by the compiler. Use this when there's no useful work
 * to interleave with the 39-cycle divide. */
static inline uint32_t divu_u32(uint32_t dividend, uint32_t divisor) {
    uint32_t result;
    __asm__ __volatile__ (
        "mov #-128, r1\n\t"
        "add r1, r1\n\t"                /* r1 = 0xFFFFFF00 */
        "mov.l %1, @(0, r1)\n\t"         /* set divisor */
        "mov #0, r2\n\t"
        "mov.l r2, @(16, r1)\n\t"        /* dividend high = 0 */
        "mov.l %2, @(20, r1)\n\t"        /* dividend low → starts divide */
        "mov.l @(20, r1), %0\n\t"        /* read quotient (stalls if !done) */
        : "=r"(result) : "r"(divisor), "r"(dividend) : "r1", "r2"
    );
    return result;
}

/* Latency-hiding form. Issue a divu_start_u32, do other work for
 * 39+ cycles, then call divu_read. d32xr uses this exact pattern in
 * r_phase6.c without a "memory" clobber — asm volatile pairs stay
 * in program order under GCC's actual behavior even when docs say
 * they "may be reordered". If we ever see wall glitches that bisect
 * to this commit, add "memory" to both clobbers. */
static inline void divu_start_u32(uint32_t dividend, uint32_t divisor) {
    __asm__ __volatile__ (
        "mov #-128, r1\n\t"
        "add r1, r1\n\t"
        "mov.l %0, @(0, r1)\n\t"
        "mov #0, r2\n\t"
        "mov.l r2, @(16, r1)\n\t"
        "mov.l %1, @(20, r1)\n\t"
        : : "r"(divisor), "r"(dividend) : "r1", "r2"
    );
}

static inline uint32_t divu_read(void) {
    uint32_t result;
    __asm__ __volatile__ (
        "mov #-128, r1\n\t"
        "add r1, r1\n\t"
        "mov.l @(20, r1), %0\n\t"
        : "=r"(result) : : "r1"
    );
    return result;
}

/* Top 32 bits of a 32×32→64-bit signed product. Maps to one DMULS.L
 * + STS MACH (~4 cycles) instead of GCC's software 64-bit multiply
 * which it falls back to when it doesn't recognize the
 * `(int)(((int64_t)a * b) >> 32)` idiom. Drops the ceiling_grid
 * per-grid-crossing math from ~30 cycles to ~4 — at 5000+ crossings
 * per frame that's ~6 ms saved per CPU. */
static inline int32_t mul_hi32_s(int32_t a, int32_t b) {
    int32_t hi;
    __asm__ (
        "dmuls.l %1, %2\n\t"
        "sts mach, %0\n\t"
        : "=r"(hi) : "r"(a), "r"(b) : "mach", "macl"
    );
    return hi;
}

/* SH-2 test-and-set for cross-CPU mutex. tas.b @Rn reads the byte,
 * sets T = 1 if it was 0 (we got the lock), sets T = 0 if it was
 * non-zero (someone else holds it), and always sets the byte's MSB
 * to 1 — atomically, via a bus-lock signal that prevents the OTHER
 * SH-2 from interleaving its own read-modify-write. movt copies the
 * T bit to a register so we can branch on it.
 *
 * The lock byte MUST live in memory accessed via the 0x20000000
 * cache-through alias — otherwise each CPU's cache caches the byte
 * separately and the "atomic" guarantee only holds within one CPU.
 * Use SHARED_UC field references for shared locks. */
static inline int sh2_try_tas(volatile uint8_t *addr) {
    int got;
    __asm__ __volatile__ (
        "tas.b @%1\n\t"
        "movt %0\n\t"
        : "=r"(got) : "r"(addr)
    );
    return got;
}

static inline void sh2_spin_tas(volatile uint8_t *addr) {
    while (!sh2_try_tas(addr));
}

static inline void sh2_release_tas(volatile uint8_t *addr) {
    *addr = 0;
}

/* SH-2 cache control — invalidate a single 16-byte cache line in one
 * write. ORing the target address with 0x40000000 selects the cache
 * "purge area" — the value written is ignored; only the address tag
 * matters. Costs ~2 cycles. Much cheaper than Mars_ClearCache() which
 * disables the cache, sets the CP+CE bits, and reinitializes the
 * whole 4 KB.
 *
 * Use when: one CPU has previously *read* a value via the cached
 * SDRAM alias and the other CPU is about to write a new value at the
 * same address. The reader invalidates its cache line so its next
 * read picks up the writer's fresh data. (Writes via the cached
 * alias already reach memory immediately — SH-2 cache is write-
 * through — so flushes are NOT needed before a remote read.)
 *
 * Adapted from d32xr's marshw.h:75. */
#define Mars_ClearCacheLine(addr) \
    (*(volatile uintptr_t *)(((uintptr_t)(addr)) | 0x40000000) = 0)

/* Invalidate `nlines` consecutive 16-byte cache lines starting at
 * `paddr`. Useful when one CPU is about to read a buffer the other
 * just wrote — single sweep of writes to the purge alias. */
static inline void Mars_ClearCacheLines(void *paddr, uint32_t nlines) {
    uintptr_t addr = ((uintptr_t)paddr) | 0x40000000;
    for (uint32_t l = 0; l < nlines; l++) {
        *(volatile uintptr_t *)addr = 0;
        addr += 16;
    }
}

#endif
