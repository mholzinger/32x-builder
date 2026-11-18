#include "box_transform.h"
#include <sh2zam/batch_experimental.h>

bool box_transform_prepared(sh2z_vec3_t *out, sh2z_vec3_t *scratch,
    const int32_t (*vertices)[3], size_t count,
    const int32_t right[3], const int32_t up[3],
    const int32_t back[3], const int32_t position[3], unsigned kernel) {
    /* Keep +back in the matrix and negate depth AFTER rounding. Negating
     * coefficients first changes the old camera convention's rounding. */
    sh2z_mat34_t matrix = {{{right[0], right[1], right[2], 0},
                           {up[0], up[1], up[2], 0},
                           {back[0], back[1], back[2], 0}}};
    sh2z_mesh_view_t view;
    sh2z_transform_plan_t plan;
    for (size_t i = 0; i < count; ++i) {
        scratch[i].x = vertices[i][0] - position[0];
        scratch[i].y = vertices[i][1] - position[1];
        scratch[i].z = vertices[i][2] - position[2];
    }
    if (!sh2z_mesh_view_init(&view, scratch, count) ||
        !sh2z_transform_plan_init(&plan, &view, &matrix)) return false;
    bool ok;
    if (kernel == 1) ok = sh2z_transform_plan_execute_mac(out, &plan, 0, count);
    else if (kernel == 2) ok = sh2z_transform_plan_execute_dmuls(out, &plan, 0, count);
    else ok = sh2z_transform_plan_execute(out, &plan);
    if (!ok) return false;
    for (size_t i = 0; i < count; ++i) out[i].z = -out[i].z;
    return true;
}
