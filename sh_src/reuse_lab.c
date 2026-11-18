/* Larger actual mesh workloads. The retired hero is benchmark-only. */
#include "reuse_lab.h"
#include "chair_model.h"
const char *const rl_names[RL_KERNELS]={"GENERAL","BATCH","MAC-PREP","REUSE-UPD","REUSE-COLD","REUSE-EXEC"};
static int32_t floor8(int32_t v){return v>=0?v/256:-1-((-1-v)/256);}
static bool bind_mesh(reuse_lab *w) {
 return sh2z_reuse_mesh_init(&w->reuse,w->in,w->count,w->indices,RL_MAX,w->keys,RL_KEYS,w->yz,RL_KEYS)&&
  sh2z_reuse_plan_init(&w->reuse_plan,&w->reuse,&w->matrix,w->xt,RL_KEYS,w->yzt,RL_KEYS);
}
bool rl_init(reuse_lab *w,unsigned fixture) {
 if(!w||fixture>3)return false;w->fixture=fixture;
 if(fixture==3) {
  w->count=CHAIRM_NVERTS;if(w->count>RL_MAX)return false;
  for(unsigned i=0;i<w->count;i++)w->in[i]=(sh2z_vec3_t){floor8(chairm_verts[i][0]*24576),floor8(chairm_verts[i][1]*24576),floor8(chairm_verts[i][2]*24576)};
 } else {
  unsigned model=fixture==2?3:fixture;sh2z_pos16_t raw[FL_MAX];unsigned n=fl_raw_positions(model,raw);
  unsigned copies=fixture==1?16:4,side=fixture==1?4:2;int32_t height=fl_world_height(model);w->count=copies*n;
  if(w->count>RL_MAX)return false;
  for(unsigned j=0;j<copies;j++)for(unsigned i=0;i<n;i++)w->in[j*n+i]=(sh2z_vec3_t){
   floor8(raw[i].x*height)+(int32_t)(j%side)*65536,floor8(raw[i].y*height),floor8(raw[i].z*height)+(int32_t)(j/side)*65536};
 }
 w->matrix=(sh2z_mat34_t){{{56756,-8192,31744,12345},{16384,61517,-8192,-8193},{-30000,15000,56756,200000}}};
 return sh2z_mat34_transform_points_general(w->reference,&w->matrix,w->in,w->count)&&
  sh2z_mesh_view_init(&w->mesh,w->in,w->count)&&sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&bind_mesh(w);
}
bool rl_run(reuse_lab *w,unsigned kernel) {
 switch(kernel) {
 case 0:return sh2z_mat34_transform_points_general(w->out,&w->matrix,w->in,w->count);
 case 1:return sh2z_mat34_transform_points(w->out,&w->matrix,w->in,w->count);
 case 2:return sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&sh2z_transform_plan_execute_mac(w->out,&w->plan,0,w->count);
 case 3:return sh2z_reuse_plan_update_matrix(&w->reuse_plan,&w->matrix)&&sh2z_reuse_plan_execute(w->out,&w->reuse_plan);
 case 4:return bind_mesh(w)&&sh2z_reuse_plan_execute(w->out,&w->reuse_plan);
 case 5:return sh2z_reuse_plan_execute(w->out,&w->reuse_plan);
 default:return false;
 }
}
bool rl_correct(const reuse_lab *w) {
 for(unsigned i=0;i<w->count;i++)if(w->out[i].x!=w->reference[i].x||w->out[i].y!=w->reference[i].y||w->out[i].z!=w->reference[i].z)return false;
 return true;
}
