/* Internal arithmetic helpers. MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#ifndef SH2ZAM_INTERNAL_H
#define SH2ZAM_INTERNAL_H
#include "sh2zam/sh2zam.h"
#include <limits.h>
#if defined(__sh__) && defined(__SH2__) && (!defined(SH2Z_FORCE_PORTABLE) || !SH2Z_FORCE_PORTABLE)
#define SH2Z_NATIVE_MUL 1
#else
#define SH2Z_NATIVE_MUL 0
#endif
#if defined(SH2Z_REQUIRE_SH2) && SH2Z_REQUIRE_SH2 && (!defined(__sh__) || !defined(__SH2__))
#error "SH2Z_REQUIRE_SH2 needs an SH-2 compiler target (-m2)"
#endif
#if defined(__sh__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "Saturn requires big endian (-mb)"
#endif
static inline sh2z_fx_t sh2zi_clamp(int64_t v) {
    return v>INT32_MAX ? INT32_MAX : (v<INT32_MIN ? INT32_MIN : (sh2z_fx_t)v);
}
static inline bool sh2zi_fits(int64_t v) { return v>=INT32_MIN && v<=INT32_MAX; }
static inline uint32_t sh2zi_magnitude(int32_t v) {
    return v<0 ? 0u-(uint32_t)v : (uint32_t)v;
}
static inline int64_t sh2zi_product(int32_t a,int32_t b) {
#if SH2Z_NATIVE_MUL
    int32_t hi; uint32_t lo;
    /* Single assembly block: compiler sees both clobbered MAC registers.
     * Application ISRs must preserve MACH/MACL when they use them. */
    __asm__ volatile("dmuls.l %[a], %[b]\n\tsts mach, %[hi]\n\tsts macl, %[lo]"
        : [hi] "=&r"(hi),[lo] "=&r"(lo) : [a] "r"(a),[b] "r"(b) : "mach","macl");
    return (int64_t)hi*INT64_C(4294967296)+(int64_t)lo;
#else
    return (int64_t)a*b;
#endif
}
/* Well-defined for INT64_MIN, without signed shifts or negating INT64_MIN.
 * Keep the conditional form: the branchless 64-bit extraction generated
 * substantially more SH-2 instructions in the native math-model capture. */
static inline int64_t sh2zi_floor16(int64_t v) {
    return v>=0 ? v/65536 : -1-((-1-v)/65536);
}
/* Four signed raw products could overflow int64_t BEFORE Q16 extraction.
 * Split each into floor quotient + positive remainder; sum both separately.
 * Maximum quotient sum is 2^48, so cancellation remains exact without int128.
 * Batch callers can hoist bounds proofs; a per-dot proof cost more than this
 * general path for vec3/vec4 in the native SH-2 instruction model.
 * minus is a bit mask of terms to subtract; offset is already Q16.16. */
static inline int64_t sh2zi_dot(const int32_t *a,const int32_t *b,unsigned n,unsigned minus,int32_t offset) {
    int64_t sum=offset; uint32_t rem=0;
    for(unsigned i=0;i<n;i++) {
        int64_t p=sh2zi_product(a[i],b[i]);
        if(minus & (1u<<i)) p=-p;
        int64_t q=sh2zi_floor16(p);
        sum+=q; rem+=(uint32_t)(p-q*65536);
    }
    return sum+(rem>>16);
}
static inline uint32_t sh2zi_isqrt32(uint32_t n) {
    uint32_t result=0,bit=UINT32_C(1)<<30;
    while(bit>n) bit>>=2;
    while(bit) {
        if(n>=result+bit) { n-=result+bit; result=(result>>1)+bit; }
        else result>>=1;
        bit>>=2;
    }
    return result;
}
/* Continue a 32-bit prefix square root with the sixteen low-word digits.
 * No 64-bit arithmetic remains inside either digit loop. The final digit
 * alone needs a high-word comparison; its remainder is not needed. */
static inline uint64_t sh2zi_isqrt(uint64_t n) {
    uint32_t hi=(uint32_t)(n>>32),lo=(uint32_t)n;
    if(!hi) return sh2zi_isqrt32(lo);
    uint32_t root=sh2zi_isqrt32(hi);
    uint32_t rem=hi-root*root;
    for(unsigned i=0;i<15;i++) {
        uint32_t carry=rem>>30;
        rem=(rem<<2)|(lo>>30); lo<<=2;
        uint32_t trial=(root<<2)|1u;
        root<<=1;
        if(carry || rem>=trial) { rem-=trial; root|=1u; }
    }
    uint32_t rem_hi=rem>>30;
    rem=(rem<<2)|(lo>>30);
    uint32_t trial_hi=root>>30,trial=(root<<2)|1u;
    root<<=1;
    if(rem_hi>trial_hi || (rem_hi==trial_hi && rem>=trial)) root|=1u;
    return root;
}
/* Unsigned 64/32 division by CPU division steps: (hi:lo)/divisor, truncated, for
 * hi < divisor, so the quotient fits 32 bits. Native: DIV0U, then 32 ROTCL/DIV1 pairs and
 * a final ROTCL (Renesas SH-1/SH-2/SH-DSP Software Manual, unsigned 64/32 example). One
 * indivisible asm block owns Q/M/T; DIV0U does not alter SR.S. CPU steps, not the
 * memory-mapped divider. Portable: the same restoring shift-subtract, one bit per step. */
static inline uint32_t sh2zi_udiv64_32(uint32_t hi,uint32_t lo,uint32_t divisor) {
#if SH2Z_NATIVE_MUL
#define SH2ZI_DIV_STEP "rotcl %[quotient]\n\tdiv1 %[divisor], %[remainder]\n\t"
#define SH2ZI_DIV_STEP8 SH2ZI_DIV_STEP SH2ZI_DIV_STEP SH2ZI_DIV_STEP SH2ZI_DIV_STEP \
                        SH2ZI_DIV_STEP SH2ZI_DIV_STEP SH2ZI_DIV_STEP SH2ZI_DIV_STEP
    __asm__ volatile(
        "div0u\n\t"
        SH2ZI_DIV_STEP8 SH2ZI_DIV_STEP8 SH2ZI_DIV_STEP8 SH2ZI_DIV_STEP8
        "rotcl %[quotient]"
        : [remainder] "+&r"(hi), [quotient] "+&r"(lo)
        : [divisor] "r"(divisor) : "t");
#undef SH2ZI_DIV_STEP8
#undef SH2ZI_DIV_STEP
    return lo;
#else
    uint32_t quotient=0;
    for(unsigned i=0;i<32;i++) {
        uint32_t carry=hi>>31;
        hi=(hi<<1)|(lo>>31); lo<<=1; quotient<<=1;
        if(carry || hi>=divisor) { hi-=divisor; quotient|=1u; }
    }
    return quotient;
#endif
}
/* clamp((a*65536)/b) for b != 0, without a 64-bit divide. C division truncates toward
 * zero, which is the magnitude quotient with the sign applied. |a|*65536 is a 48-bit
 * dividend; when its high word reaches |b| the quotient needs more than 32 bits and
 * saturates as the clamp would. Used by sh2z_div on SH-2; host tests run it portably. */
static inline int32_t sh2zi_div16(int32_t a,int32_t b) {
    const uint32_t ua=sh2zi_magnitude(a),ub=sh2zi_magnitude(b);
    const int negative=(a<0)!=(b<0);
    if((ua>>16)>=ub) return negative ? INT32_MIN : INT32_MAX;
    const uint32_t q=sh2zi_udiv64_32(ua>>16,ua<<16,ub);
    if(!negative) return q>(uint32_t)INT32_MAX ? INT32_MAX : (int32_t)q;
    return q>=UINT32_C(0x80000000) ? INT32_MIN : (int32_t)(0u-q);
}
/* Exact sixteen-bit fractional quotient for value <= denominator <= 2^31.
 * Each doubled remainder fits uint32_t, including denominator == 2^31. */
static inline uint32_t sh2zi_fraction16(uint32_t value,uint32_t denominator) {
    if(value==denominator) return 65536;
    if(!value) return 0;
    uint32_t result=0;
#if SH2Z_NATIVE_MUL
    /* value < denominator, so the quotient needs exactly sixteen fractional
     * bits and result never shifts a 1 out of its top bit. Each ROTCL therefore
     * supplies the next zero numerator bit to DIV1 and keeps its quotient bit.
     * One indivisible asm block owns Q/M/T throughout; DIV0U does not alter SR.S.
     * See Renesas SH-1/SH-2/SH-DSP Software Manual 6.1.18/19 and 6.1.47.
     * This uses CPU division steps, not the memory-mapped divider. */
    /* Spell the steps out so GCC sees the real block length when choosing
     * short SH-2 branches around the inlined sequence. */
#define SH2ZI_FRACTION_STEP "rotcl %[quotient]\n\tdiv1 %[divisor], %[remainder]\n\t"
    __asm__ volatile(
        "div0u\n\t"
        SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP
        SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP
        SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP
        SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP SH2ZI_FRACTION_STEP
        "rotcl %[quotient]"
        : [remainder] "+&r"(value), [quotient] "+&r"(result)
        : [divisor] "r"(denominator) : "t");
#undef SH2ZI_FRACTION_STEP
#else
    for(unsigned i=0;i<16;i++) {
        value<<=1; result<<=1;
        if(value>=denominator) { value-=denominator; result|=1u; }
    }
#endif
    return result;
}
#endif
