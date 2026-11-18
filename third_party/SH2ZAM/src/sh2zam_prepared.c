/* SH2ZAM - Saturn-native fixed-point mathematics.
 * Copyright (c) 2026 SH2ZAM Contributors. MIT; see root LICENSE.
 */
#include "sh2zam_prepared_internal.h"

static uint32_t magnitude(int32_t value) {
    return value<0 ? 0u-(uint32_t)value : (uint32_t)value;
}

bool sh2z_mesh_view_init(sh2z_mesh_view_t *view,
                         const sh2z_vec3_t *positions,size_t count) {
    if(!view||count>SIZE_MAX/sizeof(*positions)||(!positions&&count!=0))
        return false;
    if(sh2zi_prepared_overlap(view,sizeof(*view),positions,
                             count*sizeof(*positions)))return false;
    sh2z_mesh_view_t prepared={positions,count,{0,0,0}};
    for(size_t i=0;i<count;++i) {
        const uint32_t x=magnitude(positions[i].x);
        const uint32_t y=magnitude(positions[i].y);
        const uint32_t z=magnitude(positions[i].z);
        if(x>prepared.max_abs[0])prepared.max_abs[0]=x;
        if(y>prepared.max_abs[1])prepared.max_abs[1]=y;
        if(z>prepared.max_abs[2])prepared.max_abs[2]=z;
    }
    *view=prepared;
    return true;
}

static bool row_is_bounded(const int32_t row[4],const uint32_t max_abs[3]) {
    /* The absolute raw sum, including the integral Q16 translation, must stay
     * below 2^47. Every partial therefore fits int64_t and floor(raw/2^16)
     * fits int32_t. Keep a decreasing budget instead of adding up products:
     * three full-range products could exceed even the signed int64_t range. */
    uint64_t remaining=UINT64_C(140737488355327);
    const uint64_t translation=(uint64_t)magnitude(row[3])*UINT64_C(65536);
    if(translation>remaining)return false;
    remaining-=translation;
    for(size_t j=0;j<3;++j) {
        const uint64_t term=(uint64_t)magnitude(row[j])*max_abs[j];
        if(term>remaining)return false;
        remaining-=term;
    }
    return true;
}

bool sh2z_transform_plan_init(sh2z_transform_plan_t *plan,
                              const sh2z_mesh_view_t *view,
                              const sh2z_mat34_t *matrix) {
    if(!plan||!view||!matrix||view->count>SIZE_MAX/sizeof(*view->positions)||
       (!view->positions&&view->count!=0))return false;
    if(sh2zi_prepared_overlap(plan,sizeof(*plan),view->positions,
                             view->count*sizeof(*view->positions)))return false;
    sh2z_transform_plan_t prepared;
    prepared.matrix=*matrix;
    prepared.positions=view->positions;
    prepared.count=view->count;
    prepared.fast_path=row_is_bounded(prepared.matrix.m[0],view->max_abs)&&
                       row_is_bounded(prepared.matrix.m[1],view->max_abs)&&
                       row_is_bounded(prepared.matrix.m[2],view->max_abs);
    *plan=prepared;
    return true;
}

static void bounded_transform(sh2z_vec3_t *restrict out,
                              const sh2z_mat34_t *restrict matrix,
                              const sh2z_vec3_t *restrict in,size_t count) {
    const int32_t m00=matrix->m[0][0],m01=matrix->m[0][1];
    const int32_t m02=matrix->m[0][2],m03=matrix->m[0][3];
    const int32_t m10=matrix->m[1][0],m11=matrix->m[1][1];
    const int32_t m12=matrix->m[1][2],m13=matrix->m[1][3];
    const int32_t m20=matrix->m[2][0],m21=matrix->m[2][1];
    const int32_t m22=matrix->m[2][2],m23=matrix->m[2][3];
    for(size_t i=0;i<count;++i) {
        const int32_t x=in[i].x,y=in[i].y,z=in[i].z;
        const int64_t a=(int64_t)m00*x+(int64_t)m01*y+(int64_t)m02*z;
        out[i].x=(int32_t)(sh2zi_prepared_floor16(a)+m03);
        const int64_t b=(int64_t)m10*x+(int64_t)m11*y+(int64_t)m12*z;
        out[i].y=(int32_t)(sh2zi_prepared_floor16(b)+m13);
        const int64_t c=(int64_t)m20*x+(int64_t)m21*y+(int64_t)m22*z;
        out[i].z=(int32_t)(sh2zi_prepared_floor16(c)+m23);
    }
}

bool sh2z_transform_plan_execute_range(sh2z_vec3_t *out,
                                      const sh2z_transform_plan_t *plan,
                                      size_t first,size_t count) {
    if(!sh2zi_transform_plan_range_valid(out,plan,first,count))return false;
    if(count==0)return true;
    if(!plan->fast_path)
        return sh2z_mat34_transform_points_general(out,&plan->matrix,
                                                  plan->positions+first,count);
    bounded_transform(out,&plan->matrix,plan->positions+first,count);
    return true;
}

bool sh2z_transform_plan_execute(sh2z_vec3_t *out,
                                const sh2z_transform_plan_t *plan) {
    if(!plan)return false;
    return sh2z_transform_plan_execute_range(out,plan,0,plan->count);
}
