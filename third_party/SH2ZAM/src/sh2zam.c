/* SH2ZAM scalar/vector core. MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#include "sh2zam_internal.h"

const char *sh2z_backend(void) {
#if SH2Z_NATIVE_MUL
    return "sh2-dmuls";
#else
    return "portable";
#endif
}
sh2z_fx_t sh2z_from_int(int32_t n) { return sh2zi_clamp((int64_t)n*65536); }
int32_t sh2z_to_int(sh2z_fx_t n) { return n/65536; }
sh2z_fx_t sh2z_add(sh2z_fx_t a,sh2z_fx_t b) {
    if(b>0 && a>INT32_MAX-b) return INT32_MAX;
    if(b<0 && a<INT32_MIN-b) return INT32_MIN;
    return a+b;
}
sh2z_fx_t sh2z_sub(sh2z_fx_t a,sh2z_fx_t b) {
    if(b<0 && a>INT32_MAX+b) return INT32_MAX;
    if(b>0 && a<INT32_MIN+b) return INT32_MIN;
    return a-b;
}
sh2z_fx_t sh2z_mul(sh2z_fx_t a,sh2z_fx_t b) { return sh2zi_clamp(sh2zi_floor16(sh2zi_product(a,b))); }
sh2z_fx_t sh2z_div(sh2z_fx_t a,sh2z_fx_t b) {
    if(!b) return a>0 ? INT32_MAX : (a<0 ? INT32_MIN : 0);
#if SH2Z_NATIVE_MUL
    return sh2zi_div16(a,b);
#else
    return sh2zi_clamp(((int64_t)a*65536)/b);
#endif
}
sh2z_fx_t sh2z_muldiv(sh2z_fx_t a,sh2z_fx_t b,sh2z_fx_t c) {
    int64_t p=sh2zi_product(a,b);
    if(!c) return p>0 ? INT32_MAX : (p<0 ? INT32_MIN : 0);
    return sh2zi_clamp(p/c);
}
sh2z_fx_t sh2z_neg(sh2z_fx_t a) { return a==INT32_MIN ? INT32_MAX : -a; }
sh2z_fx_t sh2z_abs(sh2z_fx_t a) { return a<0 ? sh2z_neg(a) : a; }
sh2z_fx_t sh2z_min(sh2z_fx_t a,sh2z_fx_t b) { return a<b ? a:b; }
sh2z_fx_t sh2z_max(sh2z_fx_t a,sh2z_fx_t b) { return a>b ? a:b; }
sh2z_fx_t sh2z_clamp(sh2z_fx_t a,sh2z_fx_t lo,sh2z_fx_t hi) {
    if(lo>hi) { sh2z_fx_t t=lo; lo=hi; hi=t; }
    return sh2z_min(sh2z_max(a,lo),hi);
}
sh2z_fx_t sh2z_floor(sh2z_fx_t a) { return (sh2z_fx_t)(sh2zi_floor16(a)*65536); }
sh2z_fx_t sh2z_ceil(sh2z_fx_t a) { return sh2zi_clamp(-sh2zi_floor16(-(int64_t)a)*65536); }
sh2z_fx_t sh2z_round(sh2z_fx_t a) {
    int64_t n=a;
    return sh2zi_clamp((n>=0 ? (n+32768)/65536 : -((-n+32768)/65536))*65536);
}
sh2z_fx_t sh2z_sqrt(sh2z_fx_t a) { return a<=0 ? 0 : (sh2z_fx_t)sh2zi_isqrt((uint64_t)(uint32_t)a<<16); }
sh2z_fx_t sh2z_recip(sh2z_fx_t a) { return sh2z_div(SH2Z_ONE,a); }
sh2z_fx_t sh2z_rsqrt(sh2z_fx_t a) { return a<=0 ? 0 : sh2z_recip(sh2z_sqrt(a)); }
sh2z_fx_t sh2z_lerp(sh2z_fx_t a,sh2z_fx_t b,sh2z_fx_t t) {
    return sh2zi_clamp((int64_t)a+sh2zi_floor16(((int64_t)b-a)*t));
}
sh2z_fx_t sh2z_smoothstep(sh2z_fx_t t) {
    t=sh2z_clamp(t,0,SH2Z_ONE);
    return sh2z_mul(sh2z_mul(t,t),3*SH2Z_ONE-2*t);
}

static sh2z_fx_t length_n(const int32_t *v,unsigned n) {
    uint64_t sum=0;
    for(unsigned i=0;i<n;i++) {
        uint64_t square=(uint64_t)sh2zi_product(v[i],v[i]);
        if(UINT64_MAX-sum<square) return INT32_MAX;
        sum+=square;
    }
    uint64_t length=sh2zi_isqrt(sum);
    return length>INT32_MAX ? INT32_MAX : (int32_t)length;
}
static void normalize_n(int32_t *out,const int32_t *v,unsigned n) {
    uint32_t largest=0;
    for(unsigned i=0;i<n;i++) { uint32_t a=sh2zi_magnitude(v[i]); if(a>largest) largest=a; }
    if(!largest) { for(unsigned i=0;i<n;i++) out[i]=0; return; }
    /* Bring maximum into [2^28,2^29). Enough fractional precision for tiny
     * input; four squares fit in 2^60; no length saturation for huge input. */
    int shift=-28; uint32_t m=largest;
    if(m>=(UINT32_C(1)<<16)) { m>>=16; shift+=16; }
    if(m>=(UINT32_C(1)<<8)) { m>>=8; shift+=8; }
    if(m>=(UINT32_C(1)<<4)) { m>>=4; shift+=4; }
    if(m>=(UINT32_C(1)<<2)) { m>>=2; shift+=2; }
    if(m>=2) ++shift;
    int32_t scaled[4]; uint64_t sum=0;
    for(unsigned i=0;i<n;i++) {
        uint32_t magnitude=sh2zi_magnitude(v[i]);
        magnitude=shift>=0 ? magnitude>>shift : magnitude<<(-shift);
        scaled[i]=v[i]<0 ? -(int32_t)magnitude : (int32_t)magnitude;
        sum+=(uint64_t)sh2zi_product(scaled[i],scaled[i]);
    }
    uint32_t len=(uint32_t)sh2zi_isqrt(sum);
    for(unsigned i=0;i<n;i++) {
        int32_t quotient=(int32_t)sh2zi_fraction16(sh2zi_magnitude(scaled[i]),len);
        out[i]=scaled[i]<0 ? -quotient : quotient;
    }
}
sh2z_vec2_t sh2z_vec2_add(sh2z_vec2_t a,sh2z_vec2_t b) { return (sh2z_vec2_t){sh2z_add(a.x,b.x),sh2z_add(a.y,b.y)}; }
sh2z_vec2_t sh2z_vec2_sub(sh2z_vec2_t a,sh2z_vec2_t b) { return (sh2z_vec2_t){sh2z_sub(a.x,b.x),sh2z_sub(a.y,b.y)}; }
sh2z_vec2_t sh2z_vec2_scale(sh2z_vec2_t a,sh2z_fx_t s) { return (sh2z_vec2_t){sh2z_mul(a.x,s),sh2z_mul(a.y,s)}; }
sh2z_fx_t sh2z_vec2_dot(sh2z_vec2_t a,sh2z_vec2_t b) {
    /* Two signed 32-bit products sum within int64_t except when both are
     * INT32_MIN*INT32_MIN. That unique overflow case saturates positively. */
    if(a.x==INT32_MIN && a.y==INT32_MIN && b.x==INT32_MIN && b.y==INT32_MIN) return INT32_MAX;
    return sh2zi_clamp(sh2zi_floor16(sh2zi_product(a.x,b.x)+sh2zi_product(a.y,b.y)));
}
sh2z_fx_t sh2z_vec2_length(sh2z_vec2_t a) { const int32_t x[]={a.x,a.y}; return length_n(x,2); }
sh2z_vec2_t sh2z_vec2_normalize(sh2z_vec2_t a) { const int32_t x[]={a.x,a.y}; int32_t y[2]; normalize_n(y,x,2); return (sh2z_vec2_t){y[0],y[1]}; }
sh2z_vec2_t sh2z_vec2_lerp(sh2z_vec2_t a,sh2z_vec2_t b,sh2z_fx_t t) { return (sh2z_vec2_t){sh2z_lerp(a.x,b.x,t),sh2z_lerp(a.y,b.y,t)}; }
sh2z_vec3_t sh2z_vec3_add(sh2z_vec3_t a,sh2z_vec3_t b) { return (sh2z_vec3_t){sh2z_add(a.x,b.x),sh2z_add(a.y,b.y),sh2z_add(a.z,b.z)}; }
sh2z_vec3_t sh2z_vec3_sub(sh2z_vec3_t a,sh2z_vec3_t b) { return (sh2z_vec3_t){sh2z_sub(a.x,b.x),sh2z_sub(a.y,b.y),sh2z_sub(a.z,b.z)}; }
sh2z_vec3_t sh2z_vec3_scale(sh2z_vec3_t a,sh2z_fx_t s) { return (sh2z_vec3_t){sh2z_mul(a.x,s),sh2z_mul(a.y,s),sh2z_mul(a.z,s)}; }
sh2z_fx_t sh2z_vec3_dot(sh2z_vec3_t a,sh2z_vec3_t b) { const int32_t x[]={a.x,a.y,a.z},y[]={b.x,b.y,b.z}; return sh2zi_clamp(sh2zi_dot(x,y,3,0,0)); }
sh2z_fx_t sh2z_vec3_length(sh2z_vec3_t a) { const int32_t x[]={a.x,a.y,a.z}; return length_n(x,3); }
sh2z_vec3_t sh2z_vec3_normalize(sh2z_vec3_t a) { const int32_t x[]={a.x,a.y,a.z}; int32_t y[3]; normalize_n(y,x,3); return (sh2z_vec3_t){y[0],y[1],y[2]}; }
sh2z_vec3_t sh2z_vec3_lerp(sh2z_vec3_t a,sh2z_vec3_t b,sh2z_fx_t t) { return (sh2z_vec3_t){sh2z_lerp(a.x,b.x,t),sh2z_lerp(a.y,b.y,t),sh2z_lerp(a.z,b.z,t)}; }
sh2z_vec4_t sh2z_vec4_add(sh2z_vec4_t a,sh2z_vec4_t b) { return (sh2z_vec4_t){sh2z_add(a.x,b.x),sh2z_add(a.y,b.y),sh2z_add(a.z,b.z),sh2z_add(a.w,b.w)}; }
sh2z_vec4_t sh2z_vec4_sub(sh2z_vec4_t a,sh2z_vec4_t b) { return (sh2z_vec4_t){sh2z_sub(a.x,b.x),sh2z_sub(a.y,b.y),sh2z_sub(a.z,b.z),sh2z_sub(a.w,b.w)}; }
sh2z_vec4_t sh2z_vec4_scale(sh2z_vec4_t a,sh2z_fx_t s) { return (sh2z_vec4_t){sh2z_mul(a.x,s),sh2z_mul(a.y,s),sh2z_mul(a.z,s),sh2z_mul(a.w,s)}; }
sh2z_fx_t sh2z_vec4_dot(sh2z_vec4_t a,sh2z_vec4_t b) { const int32_t x[]={a.x,a.y,a.z,a.w},y[]={b.x,b.y,b.z,b.w}; return sh2zi_clamp(sh2zi_dot(x,y,4,0,0)); }
sh2z_fx_t sh2z_vec4_length(sh2z_vec4_t a) { const int32_t x[]={a.x,a.y,a.z,a.w}; return length_n(x,4); }
sh2z_vec4_t sh2z_vec4_normalize(sh2z_vec4_t a) { const int32_t x[]={a.x,a.y,a.z,a.w}; int32_t y[4]; normalize_n(y,x,4); return (sh2z_vec4_t){y[0],y[1],y[2],y[3]}; }
sh2z_vec4_t sh2z_vec4_lerp(sh2z_vec4_t a,sh2z_vec4_t b,sh2z_fx_t t) { return (sh2z_vec4_t){sh2z_lerp(a.x,b.x,t),sh2z_lerp(a.y,b.y,t),sh2z_lerp(a.z,b.z,t),sh2z_lerp(a.w,b.w,t)}; }

static inline sh2z_fx_t cross_component(int32_t ax,int32_t ay,int32_t bx,int32_t by) {
    /* The difference of two signed 32-bit products is in
     * [-2^63+2^31, 2^63-2^31], including all INT32_MIN cases. */
    return sh2zi_clamp(sh2zi_floor16(sh2zi_product(ax,by)-sh2zi_product(ay,bx)));
}
sh2z_fx_t sh2z_vec2_cross(sh2z_vec2_t a,sh2z_vec2_t b) {
    return cross_component(a.x,a.y,b.x,b.y);
}
sh2z_vec3_t sh2z_vec3_cross(sh2z_vec3_t a,sh2z_vec3_t b) {
    return (sh2z_vec3_t){
        cross_component(a.y,a.z,b.y,b.z),
        cross_component(a.z,a.x,b.z,b.x),
        cross_component(a.x,a.y,b.x,b.y)};
}
