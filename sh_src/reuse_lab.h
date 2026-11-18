#ifndef REUSE_LAB_H
#define REUSE_LAB_H
#include "furniture_lab.h"
#define RL_MAX 768
#define RL_KEYS 128
#define RL_KERNELS 6
typedef struct {
 sh2z_vec3_t in[RL_MAX],out[RL_MAX],reference[RL_MAX];
 sh2z_mat34_t matrix;
 sh2z_mesh_view_t mesh;
 sh2z_transform_plan_t plan;
 sh2z_reuse_mesh_t reuse;
 sh2z_reuse_plan_t reuse_plan;
 sh2z_reuse_index_t indices[RL_MAX];
 sh2z_fx_t keys[RL_KEYS];sh2z_reuse_yz_t yz[RL_KEYS];
 sh2z_reuse_term_t xt[RL_KEYS],yzt[RL_KEYS];
 unsigned fixture,count;
} reuse_lab;
extern const char *const rl_names[RL_KERNELS];
bool rl_init(reuse_lab *w,unsigned fixture);
bool rl_run(reuse_lab *w,unsigned kernel);
bool rl_correct(const reuse_lab *w);
#endif
