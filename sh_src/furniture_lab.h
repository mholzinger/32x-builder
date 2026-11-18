#ifndef FURNITURE_LAB_H
#define FURNITURE_LAB_H
#include <sh2zam/batch_experimental.h>
#include <sh2zam/reuse_experimental.h>
#include <sh2zam/view_experimental.h>
#define FL_MAX 80
#define FL_KERNELS 9
typedef struct {
 sh2z_vec3_t ram[FL_MAX],out[FL_MAX],reference[FL_MAX],legacy[FL_MAX];
 sh2z_mat34_t matrix;
 sh2z_mesh_view_t mesh;
 sh2z_transform_plan_t plan;
 sh2z_reuse_mesh_t reuse;
 sh2z_reuse_plan_t reuse_plan;
 sh2z_reuse_index_t indices[FL_MAX];
 sh2z_fx_t keys[FL_MAX];sh2z_reuse_yz_t yz[FL_MAX];
 sh2z_reuse_term_t xt[FL_MAX],yzt[FL_MAX];
 unsigned model,count,rom,facing;
} furniture_lab;
extern const char *const fl_names[FL_KERNELS];
bool fl_init(furniture_lab *w,unsigned model,bool rom,unsigned facing);
bool fl_run(furniture_lab *w,unsigned kernel);
bool fl_correct(const furniture_lab *w,unsigned kernel);
const sh2z_vec3_t *fl_source(const furniture_lab *w);
unsigned fl_raw_positions(unsigned model,sh2z_pos16_t *out);
int32_t fl_world_height(unsigned model);
/* Local exact adapter; call prepare on the primary before rendering starts.
 * Immutable ROM meshes, per-call plans, no shared mutable transform. */
bool fl_prepare_pose_meshes(void);
bool fl_pose_exact(sh2z_vec3_t *out,unsigned model,int32_t world_height,
    int32_t fc,int32_t fs,int32_t dx,int32_t dz);
uint32_t fl_hash(const sh2z_vec3_t *p,unsigned n);
#endif
