/* Share exact quaternion setup across an entire vector batch.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#include "sh2zam/rotation_batch.h"
#include "sh2zam/batch_experimental.h"
#include "sh2zam_internal.h"
#include "sh2zam_prepared_internal.h"

static bool rotation_buffers_valid(sh2z_vec3_t *out,
    const sh2z_vec3_t *in,size_t count) {
    if(count>SIZE_MAX/sizeof(*out)) return false;
    if(!count) return true;
    if(!out||!in) return false;
    const size_t bytes=count*sizeof(*out);
    return out==in||!sh2zi_prepared_overlap(out,bytes,in,bytes);
}

bool sh2z_quat_rotate_vectors(sh2z_vec3_t *out,sh2z_quat_t q,
    const sh2z_vec3_t *in,size_t count) {
    if(!rotation_buffers_valid(out,in,count)) return false;
    if(!count) return true;
    if(count==1) {
        *out=sh2z_quat_rotate(q,*in);
        return true;
    }
    const sh2z_mat34_t matrix=sh2z_quat_to_mat34(q);
    return sh2z_mat34_transform_points(out,&matrix,in,count);
}

bool sh2z_quat_rotate_vectors_mac(sh2z_vec3_t *out,sh2z_quat_t q,
    const sh2z_vec3_t *in,size_t count) {
    if(!rotation_buffers_valid(out,in,count)) return false;
    if(!count) return true;
    if(count==1) {
        *out=sh2z_quat_rotate(q,*in);
        return true;
    }
    const sh2z_mat34_t matrix=sh2z_quat_to_mat34(q);
#if SH2Z_NATIVE_MUL
    /* Eight is the smallest measured batch where bounded MAC.L recovered
     * its scan/qualification cost. Smaller batches use the ordinary path. */
    if(count>=8&&out!=in) {
        sh2z_mesh_view_t mesh;
        sh2z_transform_plan_t plan;
        if(!sh2z_mesh_view_init(&mesh,in,count)||
           !sh2z_transform_plan_init(&plan,&mesh,&matrix)) return false;
        if(sh2z_transform_plan_mac_available(&plan))
            return sh2z_transform_plan_execute_mac(out,&plan,0,count);
    }
#endif
    /* Do not pay the slower prepared scalar fallback when native MAC.L is
     * unavailable. The ordinary matrix batch retains full-range saturation
     * and exact in-place support without a persistent borrowed-input plan. */
    return sh2z_mat34_transform_points(out,&matrix,in,count);
}
