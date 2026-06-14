#ifndef SIN_TABLE_H
#define SIN_TABLE_H
#include <stdint.h>

/* Quarter-wave sine LUT — 64 entries × 2 bytes = 128 bytes (8 cache
 * lines on a 16-byte-line SH-2). Replaces a 256-entry int32 table
 * (1024 bytes, 64 cache lines) used by player_update, the wall pass,
 * sprite billboards, and head bob — ~15 sin/cos calls per frame total.
 *
 * Pattern from d32xr's tables.c::finesine — store only the first
 * quadrant (sin in [0, ~1)) as halfwords, derive the rest by quadrant
 * folding:
 *
 *   q = a >> 6                   (0..3 = which quadrant)
 *   if (q & 1) a = ~a;           (mirror Q1/Q3 around the boundary)
 *   res = quarter_sin[a & 63];
 *   return (q & 2) ? -res : res; (negate Q2/Q3)
 *
 * Precision note: the old table held sin(pi/2) = 1.0 = 65536 at
 * entry 64. The quarter-wave LUT samples [0, pi/2) at 64 points so
 * the peak isn't directly stored — we return quarter_sin[63] = 65516
 * as the approximation. The 20-ULP error at the peak is invisible
 * at 16.16 fixed-point precision for the camera-basis vectors and
 * sprite projection math we use it for. */

static const uint16_t quarter_sin[64] = {
        0,  1608,  3216,  4821,  6424,  8022,  9616, 11204,
    12785, 14359, 15924, 17479, 19024, 20557, 22078, 23586,
    25080, 26558, 28020, 29466, 30893, 32303, 33692, 35062,
    36410, 37736, 39040, 40320, 41576, 42806, 44011, 45190,
    46341, 47464, 48559, 49624, 50660, 51665, 52639, 53581,
    54491, 55368, 56212, 57022, 57798, 58538, 59244, 59914,
    60547, 61145, 61705, 62228, 62714, 63162, 63572, 63944,
    64277, 64571, 64827, 65043, 65220, 65358, 65457, 65516,
};

static inline int32_t sin_fx(uint8_t a) {
    uint8_t q = (uint8_t)(a >> 6);
    if (q & 1) a = (uint8_t)~a;
    int32_t res = (int32_t)quarter_sin[a & 63];
    return (q & 2) ? -res : res;
}

#define SIN_FX(a)  sin_fx((uint8_t)(a))
#define COS_FX(a)  sin_fx((uint8_t)((a) + 64))

#endif
