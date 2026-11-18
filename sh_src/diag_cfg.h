#ifndef DIAG_CFG_H
#define DIAG_CFG_H

#include <stdint.h>

/* DIAGNOSTIC CONFIG BLOCK
 *
 * A 32-byte array in ROM .rodata, behind the findable magic "DIAGCFG1", so a
 * measurement run is ONE binary plus a one-byte patch. No recompile means no
 * code layout shift, which is the only way an SH-2 A/B is trustworthy: layout
 * noise alone moves numbers 5-10%. This is what made the 2026-09-24 hardware
 * session possible; see devdoc/SH2-CONTENTION-AND-FRAME-ACCOUNTING.md.
 *
 * Bytes 0-7 are the magic. Flags start at 8, so a patch tool finds the magic
 * and writes at magic+index, NOT magic+8+index. Getting that wrong writes past
 * the array and silently patches whatever follows.
 *
 * `volatile` so each gate is a real ROM read rather than a folded constant.
 *
 * SHIPPING DEFAULTS ARE NOT ALL ZERO. DIAG_CEIL_DIV_HW and DIAG_QUIET_PWM
 * default to 1: they are shipped optimizations that kept their A/B byte, not
 * diagnostics. Everything else ships 0, and a ROM with any benchmark or
 * attribution byte set is never released.
 *
 * The array is full. Indices 8..31 are all assigned; a new flag needs
 * DIAG_CFG_SIZE raised here (and nowhere else, which is the point of this
 * header) plus matching zeros in the initializer in raycast.c. */

#define DIAG_CFG_SIZE   32
#define DIAG_CFG_MAGIC  "DIAGCFG1"   /* bytes 0-7; flags begin at index 8 */

enum {
    /* --- cross-CPU attribution ------------------------------------------ */
    /* Secondary skips its half and holds the bus one way for a fixed window,
     * attributing cross-CPU contention to a port.
     * 1 = park (no bus), 2 = FB word stores, 3 = SDRAM line fills. */
    DIAG_CONTENTION_PROBE   = 8,
    /* Pin the column split at 160. REQUIRED for any per-pass A/B: the adaptive
     * balancer reacts to its partner's time, so anything that changes what the
     * secondary does silently changes the primary's workload. Without this byte
     * the probe above manufactured a textbook fake 44% result. */
    DIAG_PIN_SPLIT          = 9,
    /* Metrics HUD up from boot, so a run is readable over SSH with no pad. */
    DIAG_METRICS_AT_BOOT    = 10,

    /* --- secondary audio dispatch --------------------------------------- */
    /* Disable the rate-limited fallback decode (the no-TAIL-dispatch case:
     * menus, fades, the intro). */
    DIAG_NO_FALLBACK_DECODE = 11,
    /* Decode in the TAIL safe slot instead of the legacy amb_audio_idle path. */
    DIAG_SAFE_SLOT_DECODE   = 12,

    /* --- render mode overrides ------------------------------------------ */
    /* Force a WALLS mode, encoded mode+1 so 0 still means "no override".
     * Encoding it raw made 0 mean FULL and silently benchmarked the default. */
    DIAG_FORCE_RES_MODE     = 13,
    /* Benchmark pose: 1 pins the camera standing, 2 also reports walking so the
     * motion-gated resolution paths arm. Defers to DIAG_GAMEPLAY_BENCH. */
    DIAG_BENCH_POSE         = 14,
    /* Disable combined-axis half-res (half columns no longer imply VERT rows). */
    DIAG_NO_COMBINED_AXIS   = 15,

    /* --- secondary per-pass overhead ------------------------------------ */
    /* Skip the secondary's amb_pump checkpoints. These do not shrink when
     * render work shrinks, so they come to dominate S as the resolution levers
     * cut the render, and the frame is bounded by max(H,S). */
    DIAG_SKIP_SEC_PUMP      = 16,
    /* Skip the secondary's cell-light cache purge. */
    DIAG_SKIP_SEC_PURGE     = 17,

    /* --- frame delivery -------------------------------------------------- */
    /* Bypass the pacing governor. The hold pins every in-level frame to
     * pace_target vblanks, so a render that got 24% faster still delivers at
     * the same rate until the governor steps down PACE_DOWN_RUN frames later.
     * Measuring a render change through the governor reads as "no effect". */
    DIAG_PACE_BYPASS        = 18,

    /* --- clear / occlusion ----------------------------------------------- */
    /* DISABLE the VDP auto-fill clear (the fill is the shipping default). */
    DIAG_AUTOFILL_OFF       = 19,
    /* Disable the covered-row skip, so the ceiling draws rows the walls bury. */
    DIAG_NO_COVERED_SKIP    = 20,

    /* --- viewport -------------------------------------------------------- */
    /* Viewport override for benchmarking, independent of the menu setting.
     * 1 = FRAMED, 2 = WIDE, 0 = follow shared.vp_mode. */
    DIAG_VIEWPORT_OVERRIDE  = 21,

    /* --- silhouette seams ------------------------------------------------ */
    /* Disable the sub-block seam dither (edge rounds to nearest block). */
    DIAG_NO_SEAM_DITHER     = 22,
    /* Restore the pre-VERT seam pass verbatim: full-res grid, old truncation. */
    DIAG_LEGACY_SEAM        = 23,

    /* --- adaptive resolution --------------------------------------------- */
    /* Restore the legacy AUTO controller as a unit: coupled input (governor
     * hold not subtracted), old thresholds, no dwell. It gates all three
     * together on purpose; gating only the subtraction produced an arm that
     * fired on 65% of frames and was pure noise. */
    DIAG_LEGACY_AUTO        = 24,

    /* --- SH2ZAM geometry ------------------------------------------------- */
    /* Box camera transform: 0 original, 1 prepared, 2 native MAC, 3 DMULS. */
    DIAG_BOX_TRANSFORM      = 25,
    /* Fixed box benchmark: 1..120 repeats baked pose 0..119; 0 = normal.
     * Also selects the standup angle in the gameplay benchmark's furniture
     * scenes, which is a second meaning on one byte. */
    DIAG_BOX_BENCH_POSE     = 26,
    /* Direct gameplay benchmark, bypassing boot list, menu and walk-in crawl.
     * 1 corridor, 2 room, 3 crawl, 4 pit, 5 testbed spawn,
     * 6 chair, 7 desk, 8 PVM, 9 desk+PVM. Authored maps, frozen world clock. */
    DIAG_GAMEPLAY_BENCH     = 27,
    /* Ceiling grid column scale: 0 software FX_DIV, 1 SH-2 hardware divider.
     * SHIPS AS 1. */
    DIAG_CEIL_DIV_HW        = 28,
    /* Furniture reciprocal: 0 existing asm, 1 SH2ZAM blocking, 2 split,
     * 3 exact prepared pose (requires FURNITURE_MAC_EXPERIMENT=1). */
    DIAG_FURNITURE_RECIP    = 29,
    /* Exact quiet PWM-buffer fill when every PWM voice is inactive.
     * SHIPS AS 1. */
    DIAG_QUIET_PWM          = 30,
    /* Standalone SH2ZAM math suites: 1..7 batch sizes, 8 divider. Parks CPU2.
     * 25..26 instead run the shared-geometry lab on the secondary. */
    DIAG_SH2ZAM_SUITE       = 31
};

extern const volatile uint8_t diag_cfg[DIAG_CFG_SIZE];

#endif /* DIAG_CFG_H */
