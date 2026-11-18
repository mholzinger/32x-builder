/* Inverse trig and angle helpers. MIT; (c) 2026 SH2ZAM Contributors. */
#include "sh2zam_internal.h"

int32_t sh2z_angle_delta(sh2z_angle_t a,sh2z_angle_t b) {
    return (int32_t)(((uint32_t)b-(uint32_t)a+32768u)&65535u)-32768;
}
sh2z_angle_t sh2z_angle_lerp(sh2z_angle_t a,sh2z_angle_t b,sh2z_fx_t t) {
    t=sh2z_clamp(t,0,SH2Z_ONE);
    return (sh2z_angle_t)((int32_t)a+(int32_t)(((int64_t)sh2z_angle_delta(a,b)*t)/65536));
}
sh2z_fx_t sh2z_tan(sh2z_angle_t a) {
    sh2z_fx_t s,c;
    sh2z_sincos(a,&s,&c);
    return sh2z_div(s,c);
}
sh2z_angle_t sh2z_atan(sh2z_fx_t x) { return sh2z_atan2(x,SH2Z_ONE); }
sh2z_angle_t sh2z_asin(sh2z_fx_t x) {
    x=sh2z_clamp(x,-SH2Z_ONE,SH2Z_ONE);
    uint64_t square=(uint64_t)((int64_t)x*x);
    sh2z_fx_t c=(sh2z_fx_t)sh2zi_isqrt(UINT64_C(4294967296)-square);
    return sh2z_atan2(x,c);
}
sh2z_angle_t sh2z_acos(sh2z_fx_t x) { return (sh2z_angle_t)(16384u-sh2z_asin(x)); }
