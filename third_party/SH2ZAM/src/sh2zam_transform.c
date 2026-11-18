/* SH2ZAM matrix/rotation operations. MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#include "sh2zam_internal.h"

/* If one side's unsigned magnitude sum fits 32 bits, multiplying it by any
 * int32_t components has total absolute magnitude below 2^63. Thus every
 * partial sum fits int64_t, and a single floor preserves the general dot's
 * exact rounding. The gate itself uses only checked 32-bit arithmetic. */
static inline bool sh2zi_sum2_fits(int32_t a,int32_t b) {
    return sh2zi_magnitude(a)<=UINT32_MAX-sh2zi_magnitude(b);
}
static inline bool sh2zi_sum3_fits(int32_t a,int32_t b,int32_t c) {
    uint32_t total=sh2zi_magnitude(a),next=sh2zi_magnitude(b);
    if(total>UINT32_MAX-next) return false;
    total+=next;
    return total<=UINT32_MAX-sh2zi_magnitude(c);
}
static inline bool sh2zi_sum4_fits(int32_t a,int32_t b,int32_t c,int32_t d) {
    uint32_t total=sh2zi_magnitude(a),next=sh2zi_magnitude(b);
    if(total>UINT32_MAX-next) return false;
    total+=next; next=sh2zi_magnitude(c);
    if(total>UINT32_MAX-next) return false;
    total+=next;
    return total<=UINT32_MAX-sh2zi_magnitude(d);
}
static inline int64_t sh2zi_accumulate2(const int32_t *a,int32_t x,int32_t y) {
    return sh2zi_floor16(sh2zi_product(a[0],x)+sh2zi_product(a[1],y));
}
static inline int64_t sh2zi_accumulate3(const int32_t *a,int32_t x,int32_t y,int32_t z) {
    return sh2zi_floor16(sh2zi_product(a[0],x)+sh2zi_product(a[1],y)+sh2zi_product(a[2],z));
}
static inline int64_t sh2zi_accumulate4(const int32_t *a,int32_t x,int32_t y,int32_t z,int32_t w) {
    return sh2zi_floor16(sh2zi_product(a[0],x)+sh2zi_product(a[1],y)+sh2zi_product(a[2],z)+sh2zi_product(a[3],w));
}

sh2z_mat22_t sh2z_mat22_identity(void) {
    sh2z_mat22_t m={{{0}}};
    for(unsigned i=0;i<2;i++) m.m[i][i]=SH2Z_ONE;
    return m;
}
void sh2z_mat22_multiply(sh2z_mat22_t *out,const sh2z_mat22_t *a,const sh2z_mat22_t *b) {
    sh2z_mat22_t result;
    for(unsigned row=0;row<2;row++) {
        if(sh2zi_sum2_fits(a->m[row][0],a->m[row][1])) {
            for(unsigned col=0;col<2;col++)
                result.m[row][col]=sh2zi_clamp(sh2zi_accumulate2(a->m[row],b->m[0][col],b->m[1][col]));
        } else for(unsigned col=0;col<2;col++) {
            const int32_t v[]={b->m[0][col],b->m[1][col]};
            result.m[row][col]=sh2zi_clamp(sh2zi_dot(a->m[row],v,2,0,0));
        }
    }
    *out=result;
}
sh2z_vec2_t sh2z_mat22_transform(const sh2z_mat22_t *m,sh2z_vec2_t v) {
    if(sh2zi_sum2_fits(v.x,v.y))
        return (sh2z_vec2_t){sh2zi_clamp(sh2zi_accumulate2(m->m[0],v.x,v.y)),
            sh2zi_clamp(sh2zi_accumulate2(m->m[1],v.x,v.y))};
    const int32_t x[]={v.x,v.y}; int32_t y[2];
    for(unsigned row=0;row<2;row++) y[row]=sh2zi_clamp(sh2zi_dot(m->m[row],x,2,0,0));
    return (sh2z_vec2_t){y[0],y[1]};
}
sh2z_mat33_t sh2z_mat33_identity(void) {
    sh2z_mat33_t m={{{0}}};
    for(unsigned i=0;i<3;i++) m.m[i][i]=SH2Z_ONE;
    return m;
}
void sh2z_mat33_multiply(sh2z_mat33_t *out,const sh2z_mat33_t *a,const sh2z_mat33_t *b) {
    sh2z_mat33_t result;
    for(unsigned row=0;row<3;row++) {
        if(sh2zi_sum3_fits(a->m[row][0],a->m[row][1],a->m[row][2])) {
            for(unsigned col=0;col<3;col++)
                result.m[row][col]=sh2zi_clamp(sh2zi_accumulate3(a->m[row],b->m[0][col],b->m[1][col],b->m[2][col]));
        } else for(unsigned col=0;col<3;col++) {
            const int32_t v[]={b->m[0][col],b->m[1][col],b->m[2][col]};
            result.m[row][col]=sh2zi_clamp(sh2zi_dot(a->m[row],v,3,0,0));
        }
    }
    *out=result;
}
sh2z_vec3_t sh2z_mat33_transform(const sh2z_mat33_t *m,sh2z_vec3_t v) {
    if(sh2zi_sum3_fits(v.x,v.y,v.z))
        return (sh2z_vec3_t){sh2zi_clamp(sh2zi_accumulate3(m->m[0],v.x,v.y,v.z)),
            sh2zi_clamp(sh2zi_accumulate3(m->m[1],v.x,v.y,v.z)),
            sh2zi_clamp(sh2zi_accumulate3(m->m[2],v.x,v.y,v.z))};
    const int32_t x[]={v.x,v.y,v.z}; int32_t y[3];
    for(unsigned row=0;row<3;row++) y[row]=sh2zi_clamp(sh2zi_dot(m->m[row],x,3,0,0));
    return (sh2z_vec3_t){y[0],y[1],y[2]};
}
void sh2z_mat33_transpose(sh2z_mat33_t *out,const sh2z_mat33_t *m) {
    sh2z_mat33_t result;
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) result.m[row][col]=m->m[col][row];
    *out=result;
}
sh2z_mat44_t sh2z_mat44_identity(void) {
    sh2z_mat44_t m={{{0}}};
    for(unsigned i=0;i<4;i++) m.m[i][i]=SH2Z_ONE;
    return m;
}
void sh2z_mat44_multiply(sh2z_mat44_t *out,const sh2z_mat44_t *a,const sh2z_mat44_t *b) {
    sh2z_mat44_t result;
    for(unsigned row=0;row<4;row++) {
        if(sh2zi_sum4_fits(a->m[row][0],a->m[row][1],a->m[row][2],a->m[row][3])) {
            for(unsigned col=0;col<4;col++)
                result.m[row][col]=sh2zi_clamp(sh2zi_accumulate4(a->m[row],b->m[0][col],b->m[1][col],b->m[2][col],b->m[3][col]));
        } else for(unsigned col=0;col<4;col++) {
            const int32_t v[]={b->m[0][col],b->m[1][col],b->m[2][col],b->m[3][col]};
            result.m[row][col]=sh2zi_clamp(sh2zi_dot(a->m[row],v,4,0,0));
        }
    }
    *out=result;
}
sh2z_vec4_t sh2z_mat44_transform(const sh2z_mat44_t *m,sh2z_vec4_t v) {
    if(sh2zi_sum4_fits(v.x,v.y,v.z,v.w))
        return (sh2z_vec4_t){sh2zi_clamp(sh2zi_accumulate4(m->m[0],v.x,v.y,v.z,v.w)),
            sh2zi_clamp(sh2zi_accumulate4(m->m[1],v.x,v.y,v.z,v.w)),
            sh2zi_clamp(sh2zi_accumulate4(m->m[2],v.x,v.y,v.z,v.w)),
            sh2zi_clamp(sh2zi_accumulate4(m->m[3],v.x,v.y,v.z,v.w))};
    const int32_t x[]={v.x,v.y,v.z,v.w}; int32_t y[4];
    for(unsigned row=0;row<4;row++) y[row]=sh2zi_clamp(sh2zi_dot(m->m[row],x,4,0,0));
    return (sh2z_vec4_t){y[0],y[1],y[2],y[3]};
}
void sh2z_mat44_transpose(sh2z_mat44_t *out,const sh2z_mat44_t *m) {
    sh2z_mat44_t result;
    for(unsigned row=0;row<4;row++) for(unsigned col=0;col<4;col++) result.m[row][col]=m->m[col][row];
    *out=result;
}

sh2z_mat34_t sh2z_mat34_identity(void) {
    sh2z_mat34_t m={{{SH2Z_ONE,0,0,0},{0,SH2Z_ONE,0,0},{0,0,SH2Z_ONE,0}}};
    return m;
}
void sh2z_mat34_multiply(sh2z_mat34_t *out,const sh2z_mat34_t *a,const sh2z_mat34_t *b) {
    sh2z_mat34_t result;
    for(unsigned row=0;row<3;row++) {
        if(sh2zi_sum3_fits(a->m[row][0],a->m[row][1],a->m[row][2])) {
            for(unsigned col=0;col<3;col++)
                result.m[row][col]=sh2zi_clamp(sh2zi_accumulate3(a->m[row],b->m[0][col],b->m[1][col],b->m[2][col]));
            result.m[row][3]=sh2zi_clamp(sh2zi_accumulate3(a->m[row],b->m[0][3],b->m[1][3],b->m[2][3])+a->m[row][3]);
        } else for(unsigned col=0;col<4;col++) {
            const int32_t v[]={b->m[0][col],b->m[1][col],b->m[2][col]};
            result.m[row][col]=sh2zi_clamp(sh2zi_dot(a->m[row],v,3,0,col==3?a->m[row][3]:0));
        }
    }
    *out=result;
}
sh2z_vec3_t sh2z_mat34_transform_point(const sh2z_mat34_t *m,sh2z_vec3_t v) {
    if(sh2zi_sum3_fits(v.x,v.y,v.z))
        return (sh2z_vec3_t){sh2zi_clamp(sh2zi_accumulate3(m->m[0],v.x,v.y,v.z)+m->m[0][3]),
            sh2zi_clamp(sh2zi_accumulate3(m->m[1],v.x,v.y,v.z)+m->m[1][3]),
            sh2zi_clamp(sh2zi_accumulate3(m->m[2],v.x,v.y,v.z)+m->m[2][3])};
    const int32_t x[]={v.x,v.y,v.z}; int32_t y[3];
    for(unsigned row=0;row<3;row++) y[row]=sh2zi_clamp(sh2zi_dot(m->m[row],x,3,0,m->m[row][3]));
    return (sh2z_vec3_t){y[0],y[1],y[2]};
}
sh2z_vec3_t sh2z_mat34_transform_direction(const sh2z_mat34_t *m,sh2z_vec3_t v) {
    if(sh2zi_sum3_fits(v.x,v.y,v.z))
        return (sh2z_vec3_t){sh2zi_clamp(sh2zi_accumulate3(m->m[0],v.x,v.y,v.z)),
            sh2zi_clamp(sh2zi_accumulate3(m->m[1],v.x,v.y,v.z)),
            sh2zi_clamp(sh2zi_accumulate3(m->m[2],v.x,v.y,v.z))};
    const int32_t x[]={v.x,v.y,v.z}; int32_t y[3];
    for(unsigned row=0;row<3;row++) y[row]=sh2zi_clamp(sh2zi_dot(m->m[row],x,3,0,0));
    return (sh2z_vec3_t){y[0],y[1],y[2]};
}
sh2z_mat34_t sh2z_mat34_translation(sh2z_vec3_t v) {
    sh2z_mat34_t m=sh2z_mat34_identity();
    m.m[0][3]=v.x; m.m[1][3]=v.y; m.m[2][3]=v.z;
    return m;
}
sh2z_mat34_t sh2z_mat34_scale(sh2z_vec3_t v) {
    sh2z_mat34_t m={{{0}}}; m.m[0][0]=v.x; m.m[1][1]=v.y; m.m[2][2]=v.z; return m;
}
sh2z_mat34_t sh2z_mat34_rotation_x(sh2z_angle_t a) {
    sh2z_fx_t s=sh2z_sin(a),c=sh2z_cos(a);
    sh2z_mat34_t m={{{SH2Z_ONE,0,0,0},{0,c,-s,0},{0,s,c,0}}}; return m;
}
sh2z_mat34_t sh2z_mat34_rotation_y(sh2z_angle_t a) {
    sh2z_fx_t s=sh2z_sin(a),c=sh2z_cos(a);
    sh2z_mat34_t m={{{c,0,s,0},{0,SH2Z_ONE,0,0},{-s,0,c,0}}}; return m;
}
sh2z_mat34_t sh2z_mat34_rotation_z(sh2z_angle_t a) {
    sh2z_fx_t s=sh2z_sin(a),c=sh2z_cos(a);
    sh2z_mat34_t m={{{c,-s,0,0},{s,c,0,0},{0,0,SH2Z_ONE,0}}}; return m;
}
sh2z_mat44_t sh2z_mat44_from_mat34(const sh2z_mat34_t *m) {
    sh2z_mat44_t result=sh2z_mat44_identity();
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<4;col++) result.m[row][col]=m->m[row][col];
    return result;
}
bool sh2z_mat34_look_at(sh2z_mat34_t *out,sh2z_vec3_t eye,sh2z_vec3_t target,sh2z_vec3_t up) {
    if(!out) return false;
    int64_t dx=(int64_t)target.x-eye.x,dy=(int64_t)target.y-eye.y,dz=(int64_t)target.z-eye.z;
    if(!sh2zi_fits(dx)||!sh2zi_fits(dy)||!sh2zi_fits(dz)) return false;
    sh2z_vec3_t f=sh2z_vec3_normalize((sh2z_vec3_t){(int32_t)dx,(int32_t)dy,(int32_t)dz});
    sh2z_vec3_t s=sh2z_vec3_normalize(sh2z_vec3_cross(f,sh2z_vec3_normalize(up)));
    if((!f.x&&!f.y&&!f.z)||(!s.x&&!s.y&&!s.z)) return false;
    sh2z_vec3_t u=sh2z_vec3_cross(s,f);
    sh2z_mat34_t m={{{s.x,s.y,s.z,0},{u.x,u.y,u.z,0},{-f.x,-f.y,-f.z,0}}};
    const int32_t e[]={eye.x,eye.y,eye.z};
    for(unsigned row=0;row<3;row++) {
        int64_t t=-sh2zi_dot(m.m[row],e,3,0,0);
        if(!sh2zi_fits(t)) return false;
        m.m[row][3]=(int32_t)t;
    }
    *out=m; return true;
}
bool sh2z_mat34_inverse_rigid(sh2z_mat34_t *out,const sh2z_mat34_t *m) {
    if(!out||!m) return false;
    /* Exact dot products are symmetric, so six row pairs cover all nine
     * orthonormality checks without changing the tolerance or failure rules. */
    for(unsigned row=0;row<3;row++) for(unsigned col=row;col<3;col++) {
        int64_t d=sh2zi_dot(m->m[row],m->m[col],3,0,0)-(row==col?SH2Z_ONE:0);
        if(d>32||d< -32) return false;
    }
    sh2z_mat34_t result;
    const int32_t t[]={m->m[0][3],m->m[1][3],m->m[2][3]};
    for(unsigned row=0;row<3;row++) {
        for(unsigned col=0;col<3;col++) result.m[row][col]=m->m[col][row];
        int64_t v=-sh2zi_dot(result.m[row],t,3,0,0);
        if(!sh2zi_fits(v)) return false;
        result.m[row][3]=(int32_t)v;
    }
    *out=result; return true;
}
bool sh2z_mat44_perspective(sh2z_mat44_t *out,sh2z_angle_t fov,sh2z_fx_t aspect,sh2z_fx_t near_z,sh2z_fx_t far_z) {
    if(!out||fov<2||fov>=32768u||aspect<=0||near_z<=0||far_z<=near_z) return false;
    sh2z_fx_t sine=sh2z_sin((sh2z_angle_t)(fov/2)),cosine=sh2z_cos((sh2z_angle_t)(fov/2));
    if(!sine) return false;
    int64_t y=((int64_t)cosine*65536)/sine;
    int64_t x=(y*65536)/aspect;
    int64_t d=(int64_t)far_z-near_z;
    int64_t z=-(((int64_t)far_z+near_z)*65536)/d;
    int64_t w=-(2*(int64_t)near_z*far_z)/d;
    if(!sh2zi_fits(x)||!sh2zi_fits(y)||!sh2zi_fits(z)||!sh2zi_fits(w)) return false;
    sh2z_mat44_t m={{{(int32_t)x,0,0,0},{0,(int32_t)y,0,0},{0,0,(int32_t)z,(int32_t)w},{0,0,-SH2Z_ONE,0}}};
    *out=m; return true;
}
/* Successful clip coordinates need at most 15 unsigned quotient bits.
 * Share their denominator setup and quotient loop, and start at the highest
 * possible screen-coordinate bit. No division, approximation or MMIO. */
bool sh2z_project_clip(sh2z_screen_t *out,sh2z_vec4_t v,uint16_t width,uint16_t height) {
    if(!out||!width||!height||width>32767||height>32767||v.w<=0) return false;
    if(v.x< -v.w||v.x>v.w||v.y< -v.w||v.y>v.w||v.z< -v.w||v.z>v.w) return false;
    const uint32_t denominator=2u*(uint32_t)v.w;
    uint64_t x=(uint64_t)((uint32_t)v.x+(uint32_t)v.w)*(width-1u);
    uint64_t y=(uint64_t)((uint32_t)v.w-(uint32_t)v.y)*(height-1u);
    uint64_t trial=denominator;
    uint32_t bit=1,extent=(width-1u)|(height-1u);
    if(extent>=256u) { extent>>=8; trial<<=8; bit<<=8; }
    if(extent>=16u) { extent>>=4; trial<<=4; bit<<=4; }
    if(extent>=4u) { extent>>=2; trial<<=2; bit<<=2; }
    if(extent>=2u) { trial<<=1; bit<<=1; }
    uint32_t screen_x=0,screen_y=0;
    do {
        if(x>=trial) { x-=trial; screen_x|=bit; }
        if(y>=trial) { y-=trial; screen_y|=bit; }
        trial>>=1; bit>>=1;
    } while(bit);
    out->x=(int16_t)screen_x; out->y=(int16_t)screen_y;
    return true;
}
bool sh2z_mat34_transform_points_general(sh2z_vec3_t *out,const sh2z_mat34_t *m,const sh2z_vec3_t *in,size_t count) {
    if(!count) return true;
    if(!out||!m||!in) return false;
    for(size_t i=0;i<count;i++) out[i]=sh2z_mat34_transform_point(m,in[i]);
    return true;
}
/* Bound each row once, without scanning or restricting the input vertices.
 * |coordinate| <= 2^31, so sum(|coefficient|) <= UINT32_MAX implies
 * sum(|raw product|) <= 2^63-2^31. Every partial sum then fits int64_t,
 * including products involving INT32_MIN. Translation is added AFTER the
 * single floor16, where even the largest result has ample int64_t headroom.
 * Use checked unsigned addition so the gate itself needs no 64-bit helpers. */
static bool sh2zi_batch_rows_fit(const sh2z_mat34_t *m) {
    for(unsigned row=0;row<3;row++) {
        uint32_t a=sh2zi_magnitude(m->m[row][0]);
        uint32_t b=sh2zi_magnitude(m->m[row][1]);
        uint32_t c=sh2zi_magnitude(m->m[row][2]);
        if(a>UINT32_MAX-b) return false;
        a+=b;
        if(a>UINT32_MAX-c) return false;
    }
    return true;
}
bool sh2z_mat34_transform_points(sh2z_vec3_t *out,const sh2z_mat34_t *m,const sh2z_vec3_t *in,size_t count) {
    if(!count) return true;
    if(!out||!m||!in) return false;
    if(!sh2zi_batch_rows_fit(m)) return sh2z_mat34_transform_points_general(out,m,in,count);

    /* Snapshot invariant coefficients once. The three rows are explicit so
     * there is no row loop, temporary component array or scalar function call
     * per vertex. Keep all three inputs before storing for exact in-place use. */
    const int32_t m00=m->m[0][0],m01=m->m[0][1],m02=m->m[0][2],m03=m->m[0][3];
    const int32_t m10=m->m[1][0],m11=m->m[1][1],m12=m->m[1][2],m13=m->m[1][3];
    const int32_t m20=m->m[2][0],m21=m->m[2][1],m22=m->m[2][2],m23=m->m[2][3];
    for(size_t i=0;i<count;i++) {
        const int32_t x=in[i].x,y=in[i].y,z=in[i].z;
        const int64_t row0=sh2zi_product(m00,x)+sh2zi_product(m01,y)+sh2zi_product(m02,z);
        out[i].x=sh2zi_clamp(sh2zi_floor16(row0)+m03);
        const int64_t row1=sh2zi_product(m10,x)+sh2zi_product(m11,y)+sh2zi_product(m12,z);
        out[i].y=sh2zi_clamp(sh2zi_floor16(row1)+m13);
        const int64_t row2=sh2zi_product(m20,x)+sh2zi_product(m21,y)+sh2zi_product(m22,z);
        out[i].z=sh2zi_clamp(sh2zi_floor16(row2)+m23);
    }
    return true;
}
bool sh2z_partition(size_t count,size_t worker,size_t workers,sh2z_range_t *out) {
    if(!out||!workers||worker>=workers) return false;
    size_t q=count/workers,r=count%workers;
    out->begin=q*worker+(worker<r?worker:r);
    out->end=out->begin+q+(worker<r?1:0);
    return true;
}

sh2z_quat_t sh2z_quat_identity(void) { return (sh2z_quat_t){0,0,0,SH2Z_ONE}; }
sh2z_quat_t sh2z_quat_multiply(sh2z_quat_t a,sh2z_quat_t b) {
    if(sh2zi_sum4_fits(a.w,a.x,a.y,a.z)) {
        const int64_t x=sh2zi_product(a.w,b.x)+sh2zi_product(a.x,b.w)+sh2zi_product(a.y,b.z)-sh2zi_product(a.z,b.y);
        const int64_t y=sh2zi_product(a.w,b.y)-sh2zi_product(a.x,b.z)+sh2zi_product(a.y,b.w)+sh2zi_product(a.z,b.x);
        const int64_t z=sh2zi_product(a.w,b.z)+sh2zi_product(a.x,b.y)-sh2zi_product(a.y,b.x)+sh2zi_product(a.z,b.w);
        const int64_t w=sh2zi_product(a.w,b.w)-sh2zi_product(a.x,b.x)-sh2zi_product(a.y,b.y)-sh2zi_product(a.z,b.z);
        return (sh2z_quat_t){sh2zi_clamp(sh2zi_floor16(x)),sh2zi_clamp(sh2zi_floor16(y)),
            sh2zi_clamp(sh2zi_floor16(z)),sh2zi_clamp(sh2zi_floor16(w))};
    }
    const int32_t av[]={a.w,a.x,a.y,a.z};
    const int32_t bx[]={b.x,b.w,b.z,b.y},by[]={b.y,b.z,b.w,b.x};
    const int32_t bz[]={b.z,b.y,b.x,b.w},bw[]={b.w,b.x,b.y,b.z};
    return (sh2z_quat_t){sh2zi_clamp(sh2zi_dot(av,bx,4,8,0)),sh2zi_clamp(sh2zi_dot(av,by,4,2,0)),
        sh2zi_clamp(sh2zi_dot(av,bz,4,4,0)),sh2zi_clamp(sh2zi_dot(av,bw,4,14,0))};
}
sh2z_quat_t sh2z_quat_normalize(sh2z_quat_t q) {
    if(!q.x&&!q.y&&!q.z&&!q.w) return sh2z_quat_identity();
    sh2z_vec4_t v=sh2z_vec4_normalize((sh2z_vec4_t){q.x,q.y,q.z,q.w});
    return (sh2z_quat_t){v.x,v.y,v.z,v.w};
}
sh2z_quat_t sh2z_quat_conjugate(sh2z_quat_t q) { return (sh2z_quat_t){sh2z_neg(q.x),sh2z_neg(q.y),sh2z_neg(q.z),q.w}; }
sh2z_quat_t sh2z_quat_axis_angle(sh2z_vec3_t axis,sh2z_angle_t angle) {
    axis=sh2z_vec3_normalize(axis);
    if(!axis.x&&!axis.y&&!axis.z) return sh2z_quat_identity();
    sh2z_fx_t s=sh2z_sin((sh2z_angle_t)(angle/2));
    return sh2z_quat_normalize((sh2z_quat_t){sh2z_mul(axis.x,s),sh2z_mul(axis.y,s),sh2z_mul(axis.z,s),sh2z_cos((sh2z_angle_t)(angle/2))});
}
sh2z_quat_t sh2z_quat_nlerp(sh2z_quat_t a,sh2z_quat_t b,sh2z_fx_t t) {
    a=sh2z_quat_normalize(a); b=sh2z_quat_normalize(b); t=sh2z_clamp(t,0,SH2Z_ONE);
    sh2z_vec4_t x={a.x,a.y,a.z,a.w},y={b.x,b.y,b.z,b.w};
    if(sh2z_vec4_dot(x,y)<0) y=sh2z_vec4_scale(y,-SH2Z_ONE);
    sh2z_vec4_t v=sh2z_vec4_lerp(x,y,t);
    return sh2z_quat_normalize((sh2z_quat_t){v.x,v.y,v.z,v.w});
}
sh2z_mat34_t sh2z_quat_to_mat34(sh2z_quat_t q) {
    q=sh2z_quat_normalize(q);
    int32_t xx=sh2z_mul(q.x,q.x),yy=sh2z_mul(q.y,q.y),zz=sh2z_mul(q.z,q.z);
    int32_t xy=sh2z_mul(q.x,q.y),xz=sh2z_mul(q.x,q.z),yz=sh2z_mul(q.y,q.z);
    int32_t wx=sh2z_mul(q.w,q.x),wy=sh2z_mul(q.w,q.y),wz=sh2z_mul(q.w,q.z);
    sh2z_mat34_t m={{{SH2Z_ONE-2*(yy+zz),2*(xy-wz),2*(xz+wy),0},
        {2*(xy+wz),SH2Z_ONE-2*(xx+zz),2*(yz-wx),0},
        {2*(xz-wy),2*(yz+wx),SH2Z_ONE-2*(xx+yy),0}}};
    return m;
}
sh2z_vec3_t sh2z_quat_rotate(sh2z_quat_t q,sh2z_vec3_t v) {
    sh2z_mat34_t m=sh2z_quat_to_mat34(q); return sh2z_mat34_transform_direction(&m,v);
}
