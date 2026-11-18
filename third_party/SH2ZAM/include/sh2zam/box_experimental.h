/* Exact eight-corner transforms. MIT; SH2ZAM Contributors. */
#ifndef SH2ZAM_BOX_EXPERIMENTAL_H
#define SH2ZAM_BOX_EXPERIMENTAL_H
#include "sh2zam.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Transform all corners of an ordered Q16.16 AABB. Corner index bits select
 * max X (bit 0), max Y (bit 1), max Z (bit 2); clear bits select min.
 * Exactly matches transform_points_general: floor the full dot once, add
 * translation, saturate. Supports all int32 values, including degenerate boxes.
 * Reuses endpoint products when each basis row's magnitude sum fits uint32;
 * otherwise uses the full-range scalar fallback. No allocation or MMIO.
 * All pointers require natural alignment. out has eight elements and must be
 * disjoint from matrix and box. Null, overlap or reversed bounds return false
 * before any write. Inputs may overlap each other (both are read-only).
 * Experimental: fewer products do not guarantee lower SH-2 elapsed time. */
bool sh2z_mat34_transform_box8(sh2z_vec3_t *out,
    const sh2z_mat34_t *matrix,const sh2z_aabb_t *box);
#ifdef __cplusplus
}
#endif
#endif
