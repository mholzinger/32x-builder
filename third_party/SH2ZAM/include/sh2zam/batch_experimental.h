/* Experimental prepared-batch SH-2 kernels. Not selected by default dispatch.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#ifndef SH2ZAM_BATCH_EXPERIMENTAL_H
#define SH2ZAM_BATCH_EXPERIMENTAL_H

#include "sh2zam.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Candidate availability for an initialized, immutable plan. These queries
 *  identify the implementation used by a nonempty successful execute call;
 *  they do not validate an output buffer or a requested range.
 *
 *  MAC.L additionally requires the current CPU's SR.S bit to be clear. The
 *  library reads SR and never changes its saturation/interrupt control bits.
 *  Availability must be queried on the CPU which will execute the batch.
 *  Neither availability nor cross-compilation is hardware qualification. */
bool sh2z_transform_plan_mac_available(const sh2z_transform_plan_t *plan);
bool sh2z_transform_plan_dmuls_available(const sh2z_transform_plan_t *plan);

/** Experimental alternatives to transform_plan_execute_range, with identical
 *  range, aliasing, rounding, fallback and failure contracts. The plan and its
 *  input mesh must remain immutable. Output begins at out[0].
 *
 *  Ineligible plans, non-SH-2 builds and SH2Z_FORCE_PORTABLE builds use the
 *  ordinary prepared executor. MAC.L also falls back when SR.S is set.
 *  Interrupt handlers which use MACH/MACL must preserve both registers, as
 *  required by the existing native multiplication backend. */
bool sh2z_transform_plan_execute_mac(sh2z_vec3_t *out,
    const sh2z_transform_plan_t *plan,size_t first,size_t count);
bool sh2z_transform_plan_execute_dmuls(sh2z_vec3_t *out,
    const sh2z_transform_plan_t *plan,size_t first,size_t count);

#ifdef __cplusplus
}
#endif
#endif
