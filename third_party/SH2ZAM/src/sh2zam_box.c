/* MIT; SH2ZAM Contributors. */
#include "sh2zam/box_experimental.h"
#include "sh2zam_internal.h"
#include "sh2zam_prepared_internal.h"

bool sh2z_mat34_transform_box8(sh2z_vec3_t *out,
    const sh2z_mat34_t *matrix,const sh2z_aabb_t *box) {
    if(!out||!matrix||!box)return false;
    if(sh2zi_prepared_overlap(out,8*sizeof(*out),matrix,sizeof(*matrix))||
       sh2zi_prepared_overlap(out,8*sizeof(*out),box,sizeof(*box)))return false;
    if(box->min.x>box->max.x||box->min.y>box->max.y||box->min.z>box->max.z)return false;
    bool bounded=true;
    for(unsigned r=0;r<3;r++) {
        uint32_t a=sh2zi_magnitude(matrix->m[r][0]);
        uint32_t b=sh2zi_magnitude(matrix->m[r][1]);
        uint32_t c=sh2zi_magnitude(matrix->m[r][2]);
        if(a>UINT32_MAX-b||a+b>UINT32_MAX-c)bounded=false;
    }
    if(!bounded) {
        for(unsigned i=0;i<8;i++) {
            sh2z_vec3_t v={i&1?box->max.x:box->min.x,
                i&2?box->max.y:box->min.y,i&4?box->max.z:box->min.z};
            out[i]=sh2z_mat34_transform_point(matrix,v);
        }
        return true;
    }
    /* The gate bounds every partial sum by 2^63-2^31, for ANY input.
     * Translation is added after flooring, with ample int64 headroom. */
    for(unsigned r=0;r<3;r++) {
        const int32_t *m=matrix->m[r];
        const int64_t x0=sh2zi_product(m[0],box->min.x),x1=sh2zi_product(m[0],box->max.x);
        const int64_t y0=sh2zi_product(m[1],box->min.y),y1=sh2zi_product(m[1],box->max.y);
        const int64_t z[2]={sh2zi_product(m[2],box->min.z),sh2zi_product(m[2],box->max.z)};
        const int64_t xy[4]={x0+y0,x1+y0,x0+y1,x1+y1};
        for(unsigned i=0;i<8;i++) {
            const int32_t value=sh2zi_clamp(sh2zi_floor16(xy[i&3]+z[i>>2])+m[3]);
            if(r==0)out[i].x=value;
            else if(r==1)out[i].y=value;
            else out[i].z=value;
        }
    }
    return true;
}
