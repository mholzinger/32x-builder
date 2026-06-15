#include "shared.h"

/* Lives in .data → SDRAM (the non-zero initializer forces it out of
 * .bss). Both CPUs link to the same address; access MUST go through
 * the SHARED_UC accessor in shared.h to bypass per-CPU caches.
 *
 * Defaults:
 *   amb_volume = 128  unity gain — plays ROM samples as baked. Master
 *                     can adjust at any time (0=mute, 255=hot/clip).
 * All other fields default to 0 (per C99 designated initializer
 * semantics). */
shared_t shared = {
    .amb_volume = 128,
    .step_volume = 140,
    .lighting_flags = LIGHTING_FLICKER | LIGHTING_STROBE | LIGHTING_SHIMMER,
};
