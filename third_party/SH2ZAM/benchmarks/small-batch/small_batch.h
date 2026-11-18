/* MIT; synthetic furniture-sized fixtures, no asset data. */
#ifndef SH2Z_SMALL_BATCH_H
#define SH2Z_SMALL_BATCH_H
#include <sh2zam/batch_experimental.h>
#include <sh2zam/reuse_experimental.h>
#include <sh2zam/box_experimental.h>
#include <sh2zam/divu_experimental.h>
#define SB_MAX 256
#define SB_KERNELS 9
typedef struct {
    sh2z_vec3_t in[SB_MAX],out[SB_MAX],reference[SB_MAX];
    sh2z_aabb_t boxes[SB_MAX/8];
    sh2z_mat34_t matrix;
    sh2z_mesh_view_t view;
    sh2z_transform_plan_t plan;
    sh2z_reuse_mesh_t reuse;
    sh2z_reuse_plan_t reuse_plan;
    sh2z_reuse_index_t indices[SB_MAX];
    sh2z_fx_t x_keys[SB_MAX];
    sh2z_reuse_yz_t yz_keys[SB_MAX];
    sh2z_reuse_term_t x_terms[SB_MAX],yz_terms[SB_MAX];
    size_t count;
} sb_workspace;
extern const char *const sb_names[SB_KERNELS];
bool sb_init(sb_workspace *w,size_t count);
bool sb_run(sb_workspace *w,unsigned kernel);
uint32_t sb_hash(const sh2z_vec3_t *p,size_t count);
bool sb_correct(const sb_workspace *w);
/* Returns number of failures; logical save/restore sequence, not ISR testing. */
unsigned sb_check_divu(void);
#endif
