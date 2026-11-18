#include "shared.h"

/* Lives in .data → SDRAM (the non-zero initializer forces it out of
 * .bss). Both CPUs link to the same address; access MUST go through
 * the SHARED_UC accessor in shared.h to bypass per-CPU caches.
 *
 * Defaults:
 *   amb_volume = 128  unity gain — plays ROM samples as baked. Primary
 *                     can adjust at any time (0=mute, 255=hot/clip).
 * All other fields default to 0 (per C99 designated initializer
 * semantics). */
shared_t shared = {
    .amb_volume = 128,
    .step_volume = 140,
    .lighting_flags = LIGHTING_FLICKER | LIGHTING_STROBE | LIGHTING_SHIMMER,
    .wall_halfres = 0,    /* effective flag; FULL starts at native detail */
    .wall_res_mode = 7,   /* BALANCED: full-height rows, native close/door detail;
                          * moving wall spans elsewhere use 3 samples per 4 cols.
                          * Standing is native. HALF/AUTO remain opt-in. */
    .wall_vert = 0,       /* full-height rows; only explicit reduced modes halve */
    .wall_seam_smooth = 1,/* SMOOTH silhouette by default (auto-quarter needs it); HARD is the A/B */
    .wall_dissolve = 0,   /* transient; driven by the AUTO half→full ramp */
    .wall_qdither = 1,    /* quarter-res boundary dither (spatial, static per frame) */
    .wall_lod = 0,        /* WALLS=LOD prototype off by default */
    .carpet_vlod = 1,     /* carpet vertical depth LOD on; TESTING>CARPETLOD A/Bs it */
    .hole_jamb = 1,       /* exit-hole jamb + cavity skin on; TESTING>HOLEJAMB A/Bs it */
    .auto_qtr = 0,        /* AUTO floors at half (shipped); TESTING>AUTOQTR re-arms quarter */
    .ultra_enable = 1,    /* rest-pair 60Hz flip after ~1s stillness; TESTING>ULTRA A/Bs it */
    .pace_on = 1,         /* bucket-locked frame pacing while walking; TESTING>PACE A/Bs it */
    .hero_dying = 0,      /* caveman alive until knocked down */
};
