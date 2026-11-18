/* SH2ZAM - Saturn-native fixed-point mathematics.
 * Copyright (c) 2026 SH2ZAM Contributors. MIT; see root LICENSE.
 */
#ifndef SH2ZAM_PREPARED_INTERNAL_H
#define SH2ZAM_PREPARED_INTERNAL_H
#include <sh2zam/sh2zam.h>

/* All supported targets use flat uintptr_t address ordering. Differences avoid
 * overflowing an address when checking adjacent buffers near UINTPTR_MAX. */
static inline bool sh2zi_prepared_overlap(const void *a,size_t a_bytes,
                                        const void *b,size_t b_bytes) {
    const uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(a_bytes==0||b_bytes==0)return false;
    return aa>=bb ? aa-bb<b_bytes : bb-aa<a_bytes;
}

/* Shared by portable and experimental kernels so a range cannot overwrite any
 * part of the immutable source, even a part outside the requested range. */
static inline bool sh2zi_transform_plan_range_valid(
    sh2z_vec3_t *out,const sh2z_transform_plan_t *plan,size_t first,size_t count) {
    if(!plan||first>plan->count||count>plan->count-first)return false;
    if(count==0)return true;
    if(!out||!plan->positions||plan->count>SIZE_MAX/sizeof(*out))return false;
    const size_t out_bytes=count*sizeof(*out);
    return !sh2zi_prepared_overlap(out,out_bytes,plan->positions,
                                  plan->count*sizeof(*out))&&
           !sh2zi_prepared_overlap(out,out_bytes,plan,sizeof(*plan));
}

/* Defined floor division by 2^16, including INT64_MIN. The shifted unsigned
 * value fits int64_t; subtracting the sign contribution restores floor rather
 * than implementation-defined signed shifts or truncation toward zero. */
static inline int64_t sh2zi_prepared_floor16(int64_t value) {
    const uint64_t bits=(uint64_t)value;
    return (int64_t)(bits>>16)-
           (int64_t)(bits>>63)*INT64_C(281474976710656);
}
#endif
