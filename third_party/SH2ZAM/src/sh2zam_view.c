/* Integer view transform, projection and clip codes. Portable definition of
 * the view-contract-1 results. MIT; SH2ZAM Contributors. */
#include "sh2zam/view_experimental.h"
#include "sh2zam/divu_experimental.h"

_Static_assert(sizeof(sh2z_pos16_t)==6,"sh2z_pos16_t is 6 bytes");
_Static_assert(sizeof(sh2z_view_vertex_t)==20,"sh2z_view_vertex_t is 20 bytes");

static uint32_t magnitude32(int32_t a) {
    return a<0?(uint32_t)0-(uint32_t)a:(uint32_t)a;
}
static bool row_fits(const int16_t r[3],int32_t t) {
    uint64_t sum=0;
    for(unsigned j=0;j<3;++j)sum+=magnitude32(r[j]);
    return sum*UINT64_C(32768)+magnitude32(t)<=(uint64_t)INT32_MAX;
}
static bool view_valid(const sh2z_view_t *v) {
    if(!v || v->focal_x<=0 || v->focal_y<=0 || v->near_z<=0 || v->far_z<=v->near_z ||
       v->clip[0]>v->clip[2] || v->clip[1]>v->clip[3])return false;
    for(unsigned i=0;i<3;++i)if(!row_fits(v->r[i],v->t[i]))return false;
    return true;
}
bool sh2z_view_init(sh2z_view_t *out,const sh2z_view_t *in) {
    if(!out || !view_valid(in))return false;
    if(out!=in)*out=*in;
    return true;
}
static int64_t floor_shift16(int64_t n) {
    /* Floor division by 65536 without relying on signed right shift. */
    int64_t q=n/65536;
    if(n%65536!=0 && n<0)--q;
    return q;
}
bool sh2z_view_place(sh2z_view_t *v,sh2z_vec3_t eye) {
    if(!view_valid(v))return false;
    const int64_t e[3]={eye.x,eye.y,eye.z};
    int32_t t[3];
    for(unsigned i=0;i<3;++i) {
        int64_t sum=0;
        for(unsigned j=0;j<3;++j)sum+=(int64_t)v->r[i][j]*e[j];
        const int64_t value=-floor_shift16(sum);
        if(value<INT32_MIN || value>INT32_MAX)return false;
        t[i]=(int32_t)value;
        if(!row_fits(v->r[i],t[i]))return false;
    }
    for(unsigned i=0;i<3;++i)v->t[i]=t[i];
    return true;
}
static int32_t sat32(int64_t a) {
    return a<INT32_MIN?INT32_MIN:a>INT32_MAX?INT32_MAX:(int32_t)a;
}
static int16_t sat16(int32_t a) {
    return (int16_t)(a<INT16_MIN?INT16_MIN:a>INT16_MAX?INT16_MAX:a);
}
static int32_t shift16(int32_t q) {
    /* floor(q / 65536) without implementation-defined signed shifts:
     * for negative q, floor(q / 2^16) = -((~q) >> 16) - 1. */
    return q>=0?(int32_t)((uint32_t)q>>16):-(int32_t)(~(uint32_t)q>>16)-1;
}
static bool overlaps(const void *a,size_t a_bytes,const void *b,size_t b_bytes) {
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+b_bytes && y<x+a_bytes;
}
/* Argument rules shared by every batch entry point. count > 0 here. */
static bool batch_args_ok(const sh2z_view_vertex_t *out,const void *positions,
    size_t in_stride,size_t count) {
    if(!out || !positions || in_stride<6 || (in_stride&1u) || ((uintptr_t)positions&1u))return false;
    if(count>SIZE_MAX/sizeof(*out) || (count-1)>(SIZE_MAX-6)/in_stride)return false;
    const size_t out_bytes=count*sizeof(*out),in_bytes=(count-1)*in_stride+6;
    return !((uintptr_t)out>UINTPTR_MAX-out_bytes || (uintptr_t)positions>UINTPTR_MAX-in_bytes ||
       overlaps(out,out_bytes,positions,in_bytes));
}
/* Projection and outcode of one view-space point: the batch definition after
 * the transform. Shared by every entry point that projects. */
#if defined(__GNUC__)
#define SH2ZI_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define SH2ZI_ALWAYS_INLINE inline
#endif
static SH2ZI_ALWAYS_INLINE sh2z_view_vertex_t project_point(const sh2z_view_t *c,int32_t x,int32_t y,int32_t z) {
    sh2z_view_vertex_t o={x,y,z,0,0,0,{0,0,0}};
    if(z<c->near_z) {
        o.outcode=SH2Z_OUT_NEAR;
        return o;
    }
    const int32_t qx=sat32((int64_t)c->focal_x*x/z);
    const int32_t qy=sat32((int64_t)c->focal_y*y/z);
    o.sx=sat16((int32_t)c->center_x+shift16(qx));
    o.sy=sat16((int32_t)c->center_y-shift16(qy));
    unsigned code=0;
    if(z>c->far_z)code|=SH2Z_OUT_FAR;
    if(o.sx<c->clip[0])code|=SH2Z_OUT_LEFT;
    if(o.sx>c->clip[2])code|=SH2Z_OUT_RIGHT;
    if(o.sy<c->clip[1])code|=SH2Z_OUT_TOP;
    if(o.sy>c->clip[3])code|=SH2Z_OUT_BOTTOM;
    o.outcode=(uint8_t)code;
    return o;
}
/* Out-of-line copy for the entry points that select a projector. */
static sh2z_view_vertex_t project(const sh2z_view_t *c,int32_t x,int32_t y,int32_t z) {
    return project_point(c,x,y,z);
}
static void batch_portable(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count) {
    const sh2z_view_t c=*v;
    const unsigned char *p=positions;
    for(size_t k=0;k<count;++k,p+=in_stride) {
        /* positions and in_stride are 2-byte aligned by contract. */
        const int16_t *in=(const int16_t *)(const void *)p;
        int32_t view[3];
        for(unsigned i=0;i<3;++i)
            view[i]=(int32_t)c.r[i][0]*in[0]+(int32_t)c.r[i][1]*in[1]+(int32_t)c.r[i][2]*in[2]+c.t[i];
        out[k]=project_point(&c,view[0],view[1],view[2]);
    }
}
/* trunc(magnitude * num * 65536 / den) exactly, for magnitude < 2^33,
 * 0 < den < 2^16 and num <= den (so the result is below 2^49).
 * Long division of (magnitude * num) << 16 in 16-bit digits: every step
 * divides a value below 2^32 (remainder < den < 2^16, shifted, plus a
 * digit), so only 32-bit divisions are needed, never a 64-bit one. */
static uint64_t scaled_fraction(uint64_t magnitude,uint32_t num,uint32_t den) {
    const uint64_t m=magnitude*num;          /* < 2^49 */
    const uint32_t digits[5]={(uint32_t)(m>>48)&0xFFFFu,(uint32_t)(m>>32)&0xFFFFu,
        (uint32_t)m>>16,(uint32_t)m&0xFFFFu,0u};
    uint64_t q=0;
    uint32_t r=0;
    for(unsigned i=0;i<5;++i) {
        const uint32_t cur=(r<<16)|digits[i];
        const uint32_t d=cur/den;
        r=cur-d*den;
        q=(q<<16)|d;
    }
    return q;
}
typedef sh2z_view_vertex_t (*project_fn)(const sh2z_view_t *,int32_t,int32_t,int32_t);
#if SH2Z_DIVU_NATIVE
/* project() through this CPU's divider: start x, form y's numerator while it
 * runs, collect x and start y, classify x while y runs. The divu helpers
 * answer non-fitting quotients with exactly sat32, as project() defines. */
static sh2z_view_vertex_t project_divu(const sh2z_view_t *c,int32_t x,int32_t y,int32_t z) {
    sh2z_view_vertex_t o={x,y,z,0,0,0,{0,0,0}};
    if(z<c->near_z) {
        o.outcode=SH2Z_OUT_NEAR;
        return o;
    }
    const uint64_t nx=(uint64_t)((int64_t)c->focal_x*x);
    sh2z_divu_t t;
    sh2z_divu_start64(&t,(int32_t)(uint32_t)(nx>>32),(uint32_t)nx,z);
    const uint64_t ny=(uint64_t)((int64_t)c->focal_y*y);
    const int32_t qx=sh2z_divu_finish(&t,NULL);
    sh2z_divu_start64(&t,(int32_t)(uint32_t)(ny>>32),(uint32_t)ny,z);
    o.sx=sat16((int32_t)c->center_x+shift16(qx));
    unsigned code=0;
    if(z>c->far_z)code|=SH2Z_OUT_FAR;
    if(o.sx<c->clip[0])code|=SH2Z_OUT_LEFT;
    if(o.sx>c->clip[2])code|=SH2Z_OUT_RIGHT;
    const int32_t qy=sh2z_divu_finish(&t,NULL);
    o.sy=sat16((int32_t)c->center_y-shift16(qy));
    if(o.sy<c->clip[1])code|=SH2Z_OUT_TOP;
    if(o.sy>c->clip[3])code|=SH2Z_OUT_BOTTOM;
    o.outcode=(uint8_t)code;
    return o;
}
#define PROJECT_DIVU project_divu
#else
#define PROJECT_DIVU project
#endif
/* Project view-space records in[0..count-1] into out (x y z read first, so
 * in == out is allowed). */
static void project_portable(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count) {
    const sh2z_view_t c=*v;
    for(size_t k=0;k<count;++k) {
        const int32_t x=in[k].x,y=in[k].y,z=in[k].z;
        out[k]=project_point(&c,x,y,z);
    }
}
#if SH2Z_DIVU_NATIVE
static void project_divu_native(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count);
#endif

/* One axis of the stepper, in magnitudes: acc = |F| + k*|S| and
 * P = from +/- (acc >> 16). acc <= |delta| * 65536 < 2^49 by the bound on
 * count. Each P lies between from and to, so it fits int32 and only the low
 * 32 bits of acc >> 16 are needed: modular 32-bit arithmetic gives it. */
typedef struct { uint64_t acc,step; uint32_t base; bool negative; } edge_axis_t;
#if SH2Z_DIVU_NATIVE
/* scaled_fraction on the hardware divider, for magnitude < 2^31:
 * q1 = magnitude * num / den fits strictly (it is at most magnitude), its
 * remainder r1 < den < 2^16, and (r1 << 16) / den < 2^16 fits too. */
static uint64_t scaled_fraction_divu(uint64_t magnitude,uint32_t num,uint32_t den) {
    if(magnitude>=UINT64_C(0x80000000))return scaled_fraction(magnitude,num,den);
    const uint64_t m=magnitude*num;
    const uint32_t q1=(uint32_t)sh2z_divu_div64((int64_t)m,(int32_t)den,NULL);
    const uint32_t r1=(uint32_t)(m-(uint64_t)q1*den);
    const uint32_t q2=(uint32_t)sh2z_divu_div64((int64_t)((uint64_t)r1<<16),(int32_t)den,NULL);
    return ((uint64_t)q1<<16)+q2;
}
#endif
static edge_axis_t edge_axis(int32_t from,int32_t to,uint32_t first_num,uint32_t step_num,uint32_t den,bool divu) {
    const int64_t delta=(int64_t)to-from;
    const uint64_t mag=delta<0?(uint64_t)0-(uint64_t)delta:(uint64_t)delta;
#if SH2Z_DIVU_NATIVE
    if(divu) {
        edge_axis_t e={scaled_fraction_divu(mag,first_num,den),scaled_fraction_divu(mag,step_num,den),(uint32_t)from,delta<0};
        return e;
    }
#else
    (void)divu;
#endif
    edge_axis_t e={scaled_fraction(mag,first_num,den),scaled_fraction(mag,step_num,den),(uint32_t)from,delta<0};
    return e;
}
static SH2ZI_ALWAYS_INLINE int32_t edge_next(edge_axis_t *e) {
    const uint32_t whole=((uint32_t)(e->acc>>32)<<16)|((uint32_t)e->acc>>16);
    const uint32_t sum=e->negative?e->base-whole:e->base+whole;
    e->acc+=e->step;
    return sum<=(uint32_t)INT32_MAX?(int32_t)sum:(int32_t)(sum-UINT32_C(0x80000000))-INT32_MAX-1;
}
static bool edge_steps(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    uint16_t first_num,uint16_t step_num,uint16_t den,size_t count,bool divu) {
    if(!view_valid(v))return false;
    if(!count)return true;
    if(!out || !from || !to || !den || first_num>den)return false;
    if(step_num && (count-1)>(size_t)((uint32_t)(den-first_num)/step_num))return false;
    if(count>SIZE_MAX/sizeof(*out))return false;
    /* from and to are fully read here, before out is written. */
    edge_axis_t ex=edge_axis(from->x,to->x,first_num,step_num,den,divu);
    edge_axis_t ey=edge_axis(from->y,to->y,first_num,step_num,den,divu);
    edge_axis_t ez=edge_axis(from->z,to->z,first_num,step_num,den,divu);
    for(size_t k=0;k<count;++k) {
        out[k].x=edge_next(&ex);out[k].y=edge_next(&ey);out[k].z=edge_next(&ez);
    }
#if SH2Z_DIVU_NATIVE
    if(divu) {
        project_divu_native(out,v,out,count);
        return true;
    }
#else
    (void)divu;
#endif
    project_portable(out,v,out,count);
    return true;
}

/* trunc(a * num / den) for a < 2^32 and 0 <= num <= den < 2^32, den >= 1;
 * the result is at most a. With divu on SH-2, a < 2^31 and den < 2^31 put
 * the quotient strictly inside the divider's range. */
static uint64_t crossing_offset(uint64_t a,uint64_t num,uint64_t den,bool divu) {
#if SH2Z_DIVU_NATIVE
    if(divu && a<UINT64_C(0x80000000) && den<UINT64_C(0x80000000))
        return (uint32_t)sh2z_divu_div64((int64_t)(a*num),(int32_t)den,NULL);
#else
    (void)divu;
#endif
    return a*num/den;
}
static unsigned clip_near(sh2z_view_vertex_t out[6],const sh2z_view_t *v,
    const sh2z_view_vertex_t in[4],project_fn proj,bool divu) {
    if(!view_valid(v) || !out || !in)return 0;
    sh2z_view_vertex_t q[4];
    for(unsigned i=0;i<4;++i)q[i]=in[i];
    const int32_t near_z=v->near_z;
    sh2z_view_vertex_t r[6];
    unsigned n=0;
    for(unsigned i=0;i<4;++i) {
        const sh2z_view_vertex_t *cur=&q[i],*next=&q[(i+1)&3u];
        const bool cur_in=cur->z>=near_z,next_in=next->z>=near_z;
        if(cur_in)r[n++]=*cur;
        if(cur_in!=next_in) {
            const sh2z_view_vertex_t *ins=cur_in?cur:next,*outs=cur_in?next:cur;
            /* 1 <= den < 2^32 and 0 <= num <= den, so each offset is at most
             * |outs - ins| < 2^32 and the product fits 64 unsigned bits. */
            const uint64_t num=(uint64_t)((int64_t)ins->z-near_z);
            const uint64_t den=(uint64_t)((int64_t)ins->z-outs->z);
            int32_t p[2];
            const int32_t ia[2]={ins->x,ins->y},oa[2]={outs->x,outs->y};
            for(unsigned j=0;j<2;++j) {
                const int64_t d=(int64_t)oa[j]-ia[j];
                const uint64_t mag=crossing_offset(d<0?(uint64_t)0-(uint64_t)d:(uint64_t)d,num,den,divu);
                p[j]=(int32_t)(d<0?(int64_t)ia[j]-(int64_t)mag:(int64_t)ia[j]+(int64_t)mag);
            }
            r[n++]=proj(v,p[0],p[1],near_z);
        }
    }
    for(unsigned i=0;i<n;++i)out[i]=r[i];
    return n;
}
bool sh2z_view_edge_steps(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    uint16_t first_num,uint16_t step_num,uint16_t den,size_t count) {
    return edge_steps(out,v,from,to,first_num,step_num,den,count,false);
}
bool sh2z_view_edge_steps_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    uint16_t first_num,uint16_t step_num,uint16_t den,size_t count) {
    return edge_steps(out,v,from,to,first_num,step_num,den,count,true);
}
unsigned sh2z_view_clip_near(sh2z_view_vertex_t out[6],const sh2z_view_t *v,
    const sh2z_view_vertex_t in[4]) {
    return clip_near(out,v,in,project,false);
}
unsigned sh2z_view_clip_near_divu(sh2z_view_vertex_t out[6],const sh2z_view_t *v,
    const sh2z_view_vertex_t in[4]) {
    return clip_near(out,v,in,PROJECT_DIVU,true);
}

bool sh2z_view_batch(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count) {
    if(!view_valid(v))return false;
    if(!count)return true;
    if(!batch_args_ok(out,positions,in_stride,count))return false;
    batch_portable(out,v,positions,in_stride,count);
    return true;
}

#if SH2Z_DIVU_NATIVE
/* SH-2 path of sh2z_view_batch_divu: hand-scheduled assembly, same results
 * as batch_portable byte for byte.
 *
 * Divisions. Both quotients share the divisor z >= near_z > 0. With
 * n = focal * view and focal < 2^31, |n| <= 2^62, so m = |n| >> 31 fits 32
 * bits, and the quotient fits the divider strictly (|q| < 2^31) exactly when
 * m < z. Otherwise |trunc(n / z)| >= 2^31 and the defined result is INT32_MAX
 * or INT32_MIN by the sign of n. The kernel only ever uses q >> 16 (floor),
 * and INT32_MIN >> 16 == (INT32_MIN + 1) >> 16, so an out-of-range quotient is
 * replaced by a stand-in division (+/-(2^31 - 1) * z) / z whose quotient fits
 * strictly and has the same >> 16. The main path therefore has one shape.
 *
 * Software pipeline across vertices, so the CPU works while the divider runs:
 *
 *     transform k, store x y z  (y division of k-1 still running)
 *     collect y quotient of k-1
 *     near test; start x division of k
 *     finish k-1: sy, top/bottom bits, store words 3-4
 *     y numerator of k; collect x quotient; start y division of k
 *     sx of k, far/left/right bits
 *
 * "prev" starts as, and after a NEAR vertex returns to, a scratch vertex in
 * the block, so finishing it is unconditional. Outcode bits are shifted in
 * with ROTCR in the order FAR LEFT RIGHT TOP BOTTOM; a final SHLR2 leaves
 * code << 24, which is word 4 (outcode then three zero bytes, big-endian).
 *
 * MULS.W (16x16) and DMULS.L (32x32) do not depend on SR.S. Callee-saved
 * r8-r14, PR, MACH and MACL are preserved. */
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "the SH-2 view path writes big-endian words"
#endif
typedef struct {
    int16_t r[9];                 /* z row, x row, y row */
    int16_t cx,cy,left,right,top,bottom,skip;
    int32_t t2,t0,t1,near_z,fx,fy,far_z,code;
    sh2z_view_vertex_t scratch;
} divu_block_t;
_Static_assert(offsetof(divu_block_t,cx)==18,"block cx");
_Static_assert(offsetof(divu_block_t,skip)==30,"block skip");
_Static_assert(offsetof(divu_block_t,t2)==32,"block t2");
_Static_assert(offsetof(divu_block_t,code)==60,"block code");
_Static_assert(offsetof(divu_block_t,scratch)==64,"block scratch");

void sh2zi_view_divu_run(sh2z_view_vertex_t *out,const divu_block_t *block,
    const void *positions,size_t count);
void sh2zi_view_divu_project(sh2z_view_vertex_t *out,const divu_block_t *block,
    const sh2z_view_vertex_t *in,size_t count);
static void fill_block(divu_block_t *b,const sh2z_view_t *v,int16_t skip) {
    for(unsigned j=0;j<3;++j) {
        b->r[j]=v->r[2][j];b->r[3+j]=v->r[0][j];b->r[6+j]=v->r[1][j];
    }
    b->cx=v->center_x;b->cy=v->center_y;
    b->left=v->clip[0];b->top=v->clip[1];b->right=v->clip[2];b->bottom=v->clip[3];
    b->skip=skip;
    b->t2=v->t[2];b->t0=v->t[0];b->t1=v->t[1];
    b->near_z=v->near_z;b->fx=v->focal_x;b->fy=v->focal_y;b->far_z=v->far_z;b->code=0;
}

__asm__(
/* Shared assembler macros: SH2ZI_VD_PROLOGUE saves registers and sets up
 * r2-r7 and r14; SH2ZI_VD_REST is everything after a vertex's view-space
 * x y z are in r11 r12 r13 and stored: division pipeline, classification,
 * NEAR path, stubs, finish subroutine and epilogue. tag keeps labels unique. */
"    .macro SH2ZI_VD_PROLOGUE\n"
"    mov.l   r8,@-r15\n"
"    mov.l   r9,@-r15\n"
"    mov.l   r10,@-r15\n"
"    mov.l   r11,@-r15\n"
"    mov.l   r12,@-r15\n"
"    mov.l   r13,@-r15\n"
"    mov.l   r14,@-r15\n"
"    sts.l   mach,@-r15\n"
"    sts.l   macl,@-r15\n"
"    sts.l   pr,@-r15\n"
"    mov     r5,r0\n"
"    mov     r6,r5\n"              /* r5 positions */
"    mov     r7,r6\n"              /* r6 count (> 0) */
"    mov     r0,r7\n"              /* r7 block */
"    mov     #-1,r14\n"
"    shll8   r14\n"                /* r14 = 0xFFFFFF00, divider */
"    mov     r7,r2\n"
"    add     #64,r2\n"             /* r2 prev = scratch */
"    mov     #0,r3\n"              /* r3 prev sx << 16 */
"    .endm\n"
"    .macro SH2ZI_VD_REST tag\n"
"    mov.l   @(20,r14),r8\n"       /* collect y quotient of prev */
"    mov.l   @(44,r7),r1\n"
"    cmp/ge  r1,r13\n"
"    bf      .Lsh2zi_vd_near\\tag\n"
"    mov.l   r13,@r14\n"           /* DVSR = z */
"    mov.l   @(48,r7),r1\n"
"    dmuls.l r1,r11\n"
"    sts     mach,r9\n"
"    sts     macl,r10\n"
"    mov     r9,r1\n"
"    cmp/pz  r1\n"
"    bt/s    1f\n"
"    mov     r10,r0\n"
"    negc    r0,r0\n"
"    negc    r1,r1\n"
"1:  shll    r0\n"
"    rotcl   r1\n"
"    cmp/hs  r13,r1\n"
"    bt      .Lsh2zi_vd_xsat\\tag\n"
"    mov.l   r9,@(16,r14)\n"
"    mov.l   r10,@(20,r14)\n"      /* start x */
".Lsh2zi_vd_xback\\tag:\n"
"    shlr16  r8\n"
"    exts.w  r8,r8\n"
"    mov.w   @(20,r7),r0\n"
"    sub     r8,r0\n"
"    exts.w  r0,r1\n"
"    cmp/eq  r0,r1\n"
"    bf      .Lsh2zi_vd_sysat\\tag\n"
".Lsh2zi_vd_syback\\tag:\n"
"    mov.l   @(60,r7),r9\n"
"    mov.w   @(26,r7),r0\n"
"    cmp/gt  r1,r0\n"
"    rotcr   r9\n"
"    mov.w   @(28,r7),r0\n"
"    cmp/gt  r0,r1\n"
"    rotcr   r9\n"
"    shlr2   r9\n"
"    mov.l   r9,@(16,r2)\n"
"    extu.w  r1,r1\n"
"    or      r3,r1\n"
"    mov.l   r1,@(12,r2)\n"        /* prev finished */
"    mov.l   @(52,r7),r1\n"
"    dmuls.l r1,r12\n"
"    sts     mach,r9\n"
"    sts     macl,r10\n"
"    mov     r9,r1\n"
"    cmp/pz  r1\n"
"    bt/s    2f\n"
"    mov     r10,r0\n"
"    negc    r0,r0\n"
"    negc    r1,r1\n"
"2:  shll    r0\n"
"    rotcl   r1\n"
"    cmp/hs  r13,r1\n"
"    bt/s    .Lsh2zi_vd_ysat\\tag\n"
"    mov.l   @(20,r14),r8\n"       /* collect x quotient (both paths) */
"    mov.l   r9,@(16,r14)\n"
"    mov.l   r10,@(20,r14)\n"      /* start y */
".Lsh2zi_vd_yback\\tag:\n"
"    shlr16  r8\n"
"    exts.w  r8,r8\n"
"    mov.w   @(18,r7),r0\n"
"    add     r0,r8\n"
"    exts.w  r8,r3\n"
"    cmp/eq  r8,r3\n"
"    bf      .Lsh2zi_vd_sxsat\\tag\n"
".Lsh2zi_vd_sxback\\tag:\n"
"    mov.l   @(56,r7),r1\n"
"    mov     #0,r9\n"
"    cmp/gt  r1,r13\n"
"    rotcr   r9\n"
"    mov.w   @(22,r7),r0\n"
"    cmp/gt  r3,r0\n"
"    rotcr   r9\n"
"    mov.w   @(24,r7),r0\n"
"    cmp/gt  r0,r3\n"
"    rotcr   r9\n"
"    mov.l   r9,@(60,r7)\n"
"    shll16  r3\n"
"    mov     r4,r2\n"
"    dt      r6\n"
"    bf/s    .Lsh2zi_vd_loop\\tag\n"
"    add     #20,r4\n"
"    bra     .Lsh2zi_vd_done\\tag\n"
"    nop\n"
".Lsh2zi_vd_near\\tag:\n"
"    bsr     .Lsh2zi_vd_finish\\tag\n"
"    nop\n"
"    mov     #0,r0\n"
"    mov.l   r0,@(12,r4)\n"
"    mov     #1,r0\n"
"    shll8   r0\n"
"    shll16  r0\n"
"    mov.l   r0,@(16,r4)\n"
"    mov     r7,r2\n"
"    add     #64,r2\n"
"    dt      r6\n"
"    bt/s    .Lsh2zi_vd_done\\tag\n"
"    add     #20,r4\n"
"    bra     .Lsh2zi_vd_loop\\tag\n"
"    nop\n"
".Lsh2zi_vd_xsat\\tag:\n"
"    mov     #-1,r1\n"
"    shlr    r1\n"
"    cmp/pz  r9\n"
"    bt      3f\n"
"    neg     r1,r1\n"
"3:  dmuls.l r1,r13\n"
"    sts     mach,r0\n"
"    mov.l   r0,@(16,r14)\n"
"    sts     macl,r0\n"
"    bra     .Lsh2zi_vd_xback\\tag\n"
"    mov.l   r0,@(20,r14)\n"
".Lsh2zi_vd_ysat\\tag:\n"
"    mov     #-1,r1\n"
"    shlr    r1\n"
"    cmp/pz  r9\n"
"    bt      4f\n"
"    neg     r1,r1\n"
"4:  dmuls.l r1,r13\n"
"    sts     mach,r0\n"
"    mov.l   r0,@(16,r14)\n"
"    sts     macl,r0\n"
"    bra     .Lsh2zi_vd_yback\\tag\n"
"    mov.l   r0,@(20,r14)\n"
".Lsh2zi_vd_sysat\\tag:\n"
"    mov     #-128,r1\n"
"    shll8   r1\n"
"    cmp/pz  r0\n"
"    bf      .Lsh2zi_vd_syback\\tag\n"
"    bra     .Lsh2zi_vd_syback\\tag\n"
"    not     r1,r1\n"
".Lsh2zi_vd_sxsat\\tag:\n"
"    mov     #-128,r3\n"
"    shll8   r3\n"
"    cmp/pz  r8\n"
"    bf      .Lsh2zi_vd_sxback\\tag\n"
"    bra     .Lsh2zi_vd_sxback\\tag\n"
"    not     r3,r3\n"
/* Finish prev from r8 = its y quotient, r2 = prev, r3 = its sx << 16. */
".Lsh2zi_vd_finish\\tag:\n"
"    shlr16  r8\n"
"    exts.w  r8,r8\n"
"    mov.w   @(20,r7),r0\n"
"    sub     r8,r0\n"
"    exts.w  r0,r1\n"
"    cmp/eq  r0,r1\n"
"    bt      5f\n"
"    mov     #-128,r1\n"
"    shll8   r1\n"
"    cmp/pz  r0\n"
"    bf      5f\n"
"    not     r1,r1\n"
"5:  mov.l   @(60,r7),r9\n"
"    mov.w   @(26,r7),r0\n"
"    cmp/gt  r1,r0\n"
"    rotcr   r9\n"
"    mov.w   @(28,r7),r0\n"
"    cmp/gt  r0,r1\n"
"    rotcr   r9\n"
"    shlr2   r9\n"
"    mov.l   r9,@(16,r2)\n"
"    extu.w  r1,r1\n"
"    or      r3,r1\n"
"    rts\n"
"    mov.l   r1,@(12,r2)\n"
".Lsh2zi_vd_done\\tag:\n"
"    mov.l   @(20,r14),r8\n"
"    bsr     .Lsh2zi_vd_finish\\tag\n"
"    nop\n"
"    lds.l   @r15+,pr\n"
"    lds.l   @r15+,macl\n"
"    lds.l   @r15+,mach\n"
"    mov.l   @r15+,r14\n"
"    mov.l   @r15+,r13\n"
"    mov.l   @r15+,r12\n"
"    mov.l   @r15+,r11\n"
"    mov.l   @r15+,r10\n"
"    mov.l   @r15+,r9\n"
"    rts\n"
"    mov.l   @r15+,r8\n"
"    .endm\n"
"    .text\n"
"    .align 2\n"
"    .global _sh2zi_view_divu_run\n"
"    .type _sh2zi_view_divu_run,@function\n"
"_sh2zi_view_divu_run:\n"
"    SH2ZI_VD_PROLOGUE\n"
".Lsh2zi_vd_loopT:\n"
"    mov.w   @r5+,r8\n"
"    mov.w   @r5+,r9\n"
"    mov.w   @r5+,r10\n"
"    mov.w   @(30,r7),r0\n"
"    add     r0,r5\n"
"    mov.l   @(32,r7),r13\n"
"    mov.w   @(0,r7),r0\n"
"    muls.w  r0,r8\n"
"    mov.w   @(2,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r9\n"
"    add     r1,r13\n"
"    mov.w   @(4,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r10\n"
"    add     r1,r13\n"
"    mov.l   @(36,r7),r11\n"
"    sts     macl,r1\n"
"    add     r1,r13\n"             /* vz */
"    mov.w   @(6,r7),r0\n"
"    muls.w  r0,r8\n"
"    mov.w   @(8,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r9\n"
"    add     r1,r11\n"
"    mov.w   @(10,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r10\n"
"    add     r1,r11\n"
"    mov.l   @(40,r7),r12\n"
"    sts     macl,r1\n"
"    add     r1,r11\n"             /* vx */
"    mov.w   @(12,r7),r0\n"
"    muls.w  r0,r8\n"
"    mov.w   @(14,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r9\n"
"    add     r1,r12\n"
"    mov.w   @(16,r7),r0\n"
"    sts     macl,r1\n"
"    muls.w  r0,r10\n"
"    add     r1,r12\n"
"    sts     macl,r1\n"
"    add     r1,r12\n"             /* vy */
"    mov.l   r11,@r4\n"
"    mov.l   r12,@(4,r4)\n"
"    mov.l   r13,@(8,r4)\n"
"    SH2ZI_VD_REST T\n"
"    .size _sh2zi_view_divu_run,.-_sh2zi_view_divu_run\n"
/* View-space entry: r6 points at sh2z_view_vertex_t records (stride 20);
 * x y z are read, copied to out and projected. in == out is allowed: a
 * record is read before any byte of it or of a later record is written. */
"    .align 2\n"
"    .global _sh2zi_view_divu_project\n"
"    .type _sh2zi_view_divu_project,@function\n"
"_sh2zi_view_divu_project:\n"
"    SH2ZI_VD_PROLOGUE\n"
".Lsh2zi_vd_loopP:\n"
"    mov.l   @r5,r11\n"
"    mov.l   @(4,r5),r12\n"
"    mov.l   @(8,r5),r13\n"
"    add     #20,r5\n"
"    mov.l   r11,@r4\n"
"    mov.l   r12,@(4,r4)\n"
"    mov.l   r13,@(8,r4)\n"
"    SH2ZI_VD_REST P\n"
"    .size _sh2zi_view_divu_project,.-_sh2zi_view_divu_project\n"
);

static void batch_divu_native(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count) {
    if(in_stride-6u>(size_t)INT16_MAX) {
        /* The assembly loop keeps the stride step in 16 bits. */
        batch_portable(out,v,positions,in_stride,count);
        return;
    }
    divu_block_t b;
    fill_block(&b,v,(int16_t)(in_stride-6u));
    sh2zi_view_divu_run(out,&b,positions,count);
}
static void project_divu_native(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count) {
    divu_block_t b;
    fill_block(&b,v,0);
    sh2zi_view_divu_project(out,&b,in,count);
}
#endif

bool sh2z_view_batch_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count) {
    if(!view_valid(v))return false;
    if(!count)return true;
    if(!batch_args_ok(out,positions,in_stride,count))return false;
#if SH2Z_DIVU_NATIVE
    batch_divu_native(out,v,positions,in_stride,count);
#else
    batch_portable(out,v,positions,in_stride,count);
#endif
    return true;
}

static bool project_args_ok(const sh2z_view_vertex_t *out,const sh2z_view_vertex_t *in,size_t count) {
    if(!out || !in || count>SIZE_MAX/sizeof(*out))return false;
    const size_t bytes=count*sizeof(*out);
    if((uintptr_t)out>UINTPTR_MAX-bytes || (uintptr_t)in>UINTPTR_MAX-bytes)return false;
    return out==in || !overlaps(out,bytes,in,bytes);
}
bool sh2z_view_project(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count) {
    if(!view_valid(v))return false;
    if(!count)return true;
    if(!project_args_ok(out,in,count))return false;
    project_portable(out,v,in,count);
    return true;
}
bool sh2z_view_project_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count) {
    if(!view_valid(v))return false;
    if(!count)return true;
    if(!project_args_ok(out,in,count))return false;
#if SH2Z_DIVU_NATIVE
    project_divu_native(out,v,in,count);
#else
    project_portable(out,v,in,count);
#endif
    return true;
}
