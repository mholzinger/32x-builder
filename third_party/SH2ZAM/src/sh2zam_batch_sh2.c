/* Experimental fused SH-2 prepared-batch kernels; neither is default dispatch.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#include "sh2zam/batch_experimental.h"
#include "sh2zam_internal.h"
#include "sh2zam_prepared_internal.h"

#if SH2Z_NATIVE_MUL

/* The instruction streams below rely on these public AoS/layout properties. */
_Static_assert(sizeof(sh2z_vec3_t)==3*sizeof(int32_t),"packed xyz words required");
_Static_assert(offsetof(sh2z_vec3_t,y)==sizeof(int32_t),"consecutive xyz required");
_Static_assert(offsetof(sh2z_vec3_t,z)==2*sizeof(int32_t),"consecutive xyz required");

static bool sh2zi_mac_s_clear(void) {
    uint32_t sr;
    __asm__ volatile("stc sr, %[sr]" : [sr] "=r" (sr));
    return (sr&UINT32_C(2))==0;
}

/* Each eligible plan proves, separately for every row,
 * sum(abs(m[j])*mesh.max_abs[j]) + abs(translation)*65536 <= 2^47-1.
 * Thus every partial sum fits signed 48 bits, the unshifted dot fits int32_t
 * after flooring by 65536, and adding translation cannot overflow int32_t.
 *
 * XTRCT selects bits [47:16] of MACH:MACL, exactly the two's-complement Q16.16
 * floor under this bound, including negative fractional values. No rounded
 * intermediate product, 64-bit reconstruction or saturation branch is needed.
 *
 * Reference: Renesas SH-1/SH-2/SH-DSP Software Manual, rev.5, sections 6.1.31
 * (MAC.L), 6.1.20 (DMULS.L), 6.1.3 (ADDC) and 6.1.72 (XTRCT).
 * https://www.renesas.com/en/document/mas/sh-1sh-2sh-dsp-software-manual
 *
 * Every operand read from memory is represented by an input memory operand.
 * Postincrement pointers and all scratch registers are early-clobber outputs;
 * they cannot overlap still-live inputs. No broad memory clobber is needed.
 */
static inline __attribute__((always_inline)) int32_t
sh2zi_mac_row(const int32_t row[4],const sh2z_vec3_t *point) {
    const int32_t *coefficients=row;
    const int32_t *coordinates=&point->x;
    int32_t hi,result;
    __asm__ volatile(
        "clrmac\n\t"
        "mac.l @%[coefficients]+, @%[coordinates]+\n\t"
        "mac.l @%[coefficients]+, @%[coordinates]+\n\t"
        "mac.l @%[coefficients]+, @%[coordinates]+\n\t"
        "sts mach, %[hi]\n\t"
        "sts macl, %[result]\n\t"
        "xtrct %[hi], %[result]\n\t"
        "mov.l @%[coefficients], %[hi]\n\t"
        "add %[hi], %[result]"
        : [coefficients] "+&r" (coefficients),
          [coordinates] "+&r" (coordinates),
          [hi] "=&r" (hi), [result] "=&r" (result)
        : "m" (*(const int32_t (*)[4])row), "m" (*point)
        : "mach","macl");
    return result;
}

static inline __attribute__((always_inline)) int32_t
sh2zi_dmuls_row(const sh2z_mat34_t *matrix,const int32_t row[4],
    int32_t x,int32_t y,int32_t z) {
    const int32_t *coefficients=row;
    int32_t hi,result,scratch,lo;
    __asm__ volatile(
        "mov.l @%[coefficients]+, %[scratch]\n\t"
        "dmuls.l %[scratch], %[x]\n\t"
        "sts mach, %[hi]\n\t"
        "sts macl, %[result]\n\t"
        "mov.l @%[coefficients]+, %[scratch]\n\t"
        "dmuls.l %[scratch], %[y]\n\t"
        "sts mach, %[scratch]\n\t"
        "sts macl, %[lo]\n\t"
        "clrt\n\t"
        "addc %[lo], %[result]\n\t"
        "addc %[scratch], %[hi]\n\t"
        "mov.l @%[coefficients]+, %[scratch]\n\t"
        "dmuls.l %[scratch], %[z]\n\t"
        "sts mach, %[scratch]\n\t"
        "sts macl, %[lo]\n\t"
        "clrt\n\t"
        "addc %[lo], %[result]\n\t"
        "addc %[scratch], %[hi]\n\t"
        "xtrct %[hi], %[result]\n\t"
        "mov.l @%[coefficients], %[scratch]\n\t"
        "add %[scratch], %[result]"
        : [coefficients] "+&r" (coefficients),
          [hi] "=&r" (hi), [result] "=&r" (result),
          [scratch] "=&r" (scratch), [lo] "=&r" (lo)
        : [x] "r" (x), [y] "r" (y), [z] "r" (z),
          /* The enclosing matrix models every coefficient/translation read
           * while reusing its already-live base register. A separate original
           * row address would stay live beside the postincrement pointer and
           * caused a row-pointer spill in GCC 13.2's SH-2 register allocation. */
          "m" (*matrix)
        : "mach","macl","t");
    return result;
}

/* Keep each complete loop separately visible in disassembly. The dot-row
 * helpers always inline: there are no calls or C int64_t operations inside
 * either vertex loop. Only one call is made per validated nonempty range. */
static __attribute__((noinline)) void
sh2zi_batch_mac_kernel(sh2z_vec3_t *out,const sh2z_mat34_t *matrix,
    const sh2z_vec3_t *in,size_t count) {
    for(size_t i=0;i<count;i++) {
        const int32_t x=sh2zi_mac_row(matrix->m[0],&in[i]);
        const int32_t y=sh2zi_mac_row(matrix->m[1],&in[i]);
        const int32_t z=sh2zi_mac_row(matrix->m[2],&in[i]);
        out[i]=(sh2z_vec3_t){x,y,z};
    }
}

static __attribute__((noinline)) void
sh2zi_batch_dmuls_kernel(sh2z_vec3_t *out,const sh2z_mat34_t *matrix,
    const sh2z_vec3_t *in,size_t count) {
    for(size_t i=0;i<count;i++) {
        const int32_t x=in[i].x,y=in[i].y,z=in[i].z;
        /* xyz is captured; validation rejects output overlap with matrix/input.
         * Retire each output now instead of keeping three results live through
         * the following asm blocks: SH-2 has only sixteen general registers. */
        out[i].x=sh2zi_dmuls_row(matrix,matrix->m[0],x,y,z);
        out[i].y=sh2zi_dmuls_row(matrix,matrix->m[1],x,y,z);
        out[i].z=sh2zi_dmuls_row(matrix,matrix->m[2],x,y,z);
    }
}
#endif

bool sh2z_transform_plan_mac_available(const sh2z_transform_plan_t *plan) {
#if SH2Z_NATIVE_MUL
    return plan && plan->fast_path && sh2zi_mac_s_clear();
#else
    (void)plan;
    return false;
#endif
}

bool sh2z_transform_plan_dmuls_available(const sh2z_transform_plan_t *plan) {
#if SH2Z_NATIVE_MUL
    return plan && plan->fast_path;
#else
    (void)plan;
    return false;
#endif
}

bool sh2z_transform_plan_execute_mac(sh2z_vec3_t *out,
    const sh2z_transform_plan_t *plan,size_t first,size_t count) {
    if(!sh2zi_transform_plan_range_valid(out,plan,first,count)) return false;
    if(!count) return true;
#if SH2Z_NATIVE_MUL
    if(sh2z_transform_plan_mac_available(plan)) {
        sh2zi_batch_mac_kernel(out,&plan->matrix,plan->positions+first,count);
        return true;
    }
#endif
    return sh2z_transform_plan_execute_range(out,plan,first,count);
}

bool sh2z_transform_plan_execute_dmuls(sh2z_vec3_t *out,
    const sh2z_transform_plan_t *plan,size_t first,size_t count) {
    if(!sh2zi_transform_plan_range_valid(out,plan,first,count)) return false;
    if(!count) return true;
#if SH2Z_NATIVE_MUL
    if(sh2z_transform_plan_dmuls_available(plan)) {
        sh2zi_batch_dmuls_kernel(out,&plan->matrix,plan->positions+first,count);
        return true;
    }
#endif
    return sh2z_transform_plan_execute_range(out,plan,first,count);
}
