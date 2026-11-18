/* Explicit SH-2 division unit helper. MIT; SH2ZAM Contributors. */
#include "sh2zam/divu_experimental.h"

int32_t sh2z_divu_div64(int64_t n,int32_t divisor,bool *overflow) {
    const uint64_t raw=(uint64_t)n;
    sh2z_divu_t t;
    sh2z_divu_start64(&t,(int32_t)(uint32_t)(raw>>32),(uint32_t)raw,divisor);
    return sh2z_divu_finish(&t,overflow);
}

sh2z_fx_t sh2z_divu_div16(sh2z_fx_t a,sh2z_fx_t b,bool *overflow) {
    sh2z_divu_t t;
    sh2z_divu_start16(&t,a,b);
    return sh2z_divu_finish(&t,overflow);
}
