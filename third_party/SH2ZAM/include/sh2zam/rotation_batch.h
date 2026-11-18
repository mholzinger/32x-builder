/* Exact quaternion rotation of whole vector batches.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#ifndef SH2ZAM_ROTATION_BATCH_H
#define SH2ZAM_ROTATION_BATCH_H

#include "sh2zam.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Rotate a complete vector batch by one quaternion. Results are bit-identical
 *  to calling sh2z_quat_rotate(q,in[i]) for each vector, including zero or
 *  full-range quaternions, full-range vectors, flooring and saturation.
 *
 *  Quaternion normalization and matrix construction happen once per call.
 *  A single vector calls quat_rotate directly. Larger batches use the existing
 *  exact composition quat_to_mat34(q) followed by mat34_transform_points; the
 *  generated matrix has zero translation. This API makes that shared setup
 *  explicit; the existing composition remains a performance baseline.
 *
 *  Exact in-place operation (out==in) is supported. Other input/output overlap,
 *  a byte count overflowing size_t, and nonempty null buffers return false
 *  before any output write. Zero count accepts null buffers and does no math.
 *  No allocation, persistent cache or mutable global state is used. */
bool sh2z_quat_rotate_vectors(sh2z_vec3_t *out,sh2z_quat_t q,
    const sh2z_vec3_t *in,size_t count);

/** Explicit experimental SH-2 MAC.L alternative with the same exact numeric
 *  and buffer contracts. Batches below eight vectors use the ordinary path,
 *  skipping native input-bound scanning and plan qualification. A single
 *  vector calls quat_rotate directly. When native qualification is attempted,
 *  its matrix construction, scan and plan setup are included in the call.
 *
 *  Out-of-place batches of at least eight vectors use the existing fused native
 *  kernel only when its range proof succeeds and the current CPU's SR.S bit is clear. In-place
 *  batches, unproved output ranges, SR.S=1, non-SH-2 builds and forced-portable
 *  builds use the ordinary matrix batch. SR is read and never modified.
 *  This is not the default entry point or physical-console qualification.
 *  Application ISRs using MACH/MACL must preserve both registers, as required
 *  by the library's existing native multiplication backend. */
bool sh2z_quat_rotate_vectors_mac(sh2z_vec3_t *out,sh2z_quat_t q,
    const sh2z_vec3_t *in,size_t count);

#ifdef __cplusplus
}
#endif
#endif
