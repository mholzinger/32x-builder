/* Exercise the production adapter against every baked intro/fall pose. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "box_transform.h"
#include "box_model.h"

static int32_t floor16(int64_t n) {
    return (int32_t)(n >= 0 ? n / 65536 : -1 - (-(n + 1) / 65536));
}
int main(void) {
    sh2z_vec3_t out[BOX_NVERTS], scratch[BOX_NVERTS];
    unsigned changed = 0, max_delta = 0, clip_changes = 0;
    for (int f = 0; f < BOX_NFRAMES; ++f) {
        assert(box_transform_prepared(out, scratch, box_verts[f], BOX_NVERTS,
            box_cam_right[f], box_cam_up[f], box_cam_back[f], box_cam_pos[f], 0));
        const int32_t *basis[3] = {box_cam_right[f], box_cam_up[f], box_cam_back[f]};
        for (int i = 0; i < BOX_NVERTS; ++i) {
            int32_t got[3] = {out[i].x, out[i].y, out[i].z};
            for (int r = 0; r < 3; ++r) {
                int64_t sum = 0;
                int32_t old = 0;
                for (int k = 0; k < 3; ++k) {
                    int64_t relative = (int64_t)box_verts[f][i][k] - box_cam_pos[f][k];
                    int64_t product = relative * basis[r][k];
                    sum += product;
                    old += floor16(product);
                }
                int32_t expected = floor16(sum);
                if (r == 2) { expected = -expected; old = -old; }
                assert(got[r] == expected);
                unsigned delta = (unsigned)abs(got[r] - old);
                assert(delta <= 2); /* Three floors versus one floor. */
                if (delta) ++changed;
                if (delta > max_delta) max_delta = delta;
                if (r == 2 && (got[r] < BOX_NEAR) != (old < BOX_NEAR)) ++clip_changes;
            }
        }
    }
    assert(clip_changes == 0);
    printf("%d frames, %d vertices: oracle exact; %u changed components, max delta %u/65536; near-plane classifications unchanged\n",
        BOX_NFRAMES, BOX_NFRAMES * BOX_NVERTS, changed, max_delta);
    return 0;
}
