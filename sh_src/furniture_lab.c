/* Real asset transform experiment. No shipping dispatch. */
#include "furniture_lab.h"
#include "chair3d.h"
#include "desk3d.h"
#include "pvm3d.h"
#include "desk_pvm3d.h"
#include "sin_table.h"
#include "../experiments/sh2zam-nightly/mesh_positions.h"
const char *const fl_names[FL_KERNELS]={"ENGINE","BATCH","COLD-PREP","WARM-PREP","MAC-PREP","DMUL-PREP","REUSE","EXEC-ONLY","MAC-EXACT"};
static const cbox_t *const models[]={chair_boxes,desk_boxes,pvm_boxes,desk_pvm_boxes};
static const unsigned sizes[]={CHAIR_NBOXES,DESK_NBOXES,PVM_NBOXES,DESK_PVM_NBOXES};
static const int32_t heights[]={24576,20316,26214,(26214*294/256)};
static int32_t floor_shift(int64_t n,unsigned shift) {
 const int64_t divisor=(int64_t)1<<shift;
 return (int32_t)(n>=0?n/divisor:-1-((-1-n)/divisor));
}
static int32_t mul(int32_t a,int32_t b){return floor_shift((int64_t)a*b,16);}
const sh2z_vec3_t *fl_source(const furniture_lab *w){return w->rom?fl_rom[w->model]:w->ram;}
static bool engine(furniture_lab *w) {
 const cbox_t *boxes=models[w->model];const int32_t h=heights[w->model];
 const int32_t fc=COS_FX(w->facing+64),fs=-SIN_FX(w->facing+64);
 for(unsigned b=0;b<sizes[w->model];b++)for(int v=0;v<8;v++) {
  int16_t x,y,z;cbox_corner(boxes+b,v,&x,&y,&z);
  int32_t wx=floor_shift((int32_t)x*h,8),wy=floor_shift((int32_t)y*h,8),wz=floor_shift((int32_t)z*h,8);
  w->out[b*8+(unsigned)v]=(sh2z_vec3_t){mul(wx,fc)+mul(wz,fs)+w->matrix.m[0][3],wy,
      -mul(wx,fs)+mul(wz,fc)+w->matrix.m[2][3]};
 }
 return true;
}
bool fl_init(furniture_lab *w,unsigned model,bool rom,unsigned facing) {
 if(!w||model>=4)return false;
 w->facing=facing;w->model=model;w->count=sizes[model]*8;w->rom=rom?1u:0u;
 for(unsigned i=0;i<w->count;i++)w->ram[i]=fl_rom[model][i];
 const int32_t fc=COS_FX(facing+64),fs=-SIN_FX(facing+64);
 w->matrix=(sh2z_mat34_t){{{fc,0,fs,12345},{0,65536,0,0},{-fs,0,fc,98304}}};
 if(!engine(w))return false;
 for(unsigned i=0;i<w->count;i++)w->legacy[i]=w->out[i];
 const sh2z_vec3_t *src=fl_source(w);
 return sh2z_mat34_transform_points_general(w->reference,&w->matrix,src,w->count)&&
  sh2z_mesh_view_init(&w->mesh,src,w->count)&&
  sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&
  sh2z_reuse_mesh_init(&w->reuse,src,w->count,w->indices,FL_MAX,w->keys,FL_MAX,w->yz,FL_MAX)&&
  sh2z_reuse_plan_init(&w->reuse_plan,&w->reuse,&w->matrix,w->xt,FL_MAX,w->yzt,FL_MAX);
}
bool fl_run(furniture_lab *w,unsigned kernel) {
 if(kernel>0&&kernel!=7) {
  const int32_t fc=COS_FX(w->facing+64),fs=-SIN_FX(w->facing+64);
  w->matrix=(sh2z_mat34_t){{{fc,0,fs,12345},{0,65536,0,0},{-fs,0,fc,98304}}};
 }
 switch(kernel) {
 case 0:return engine(w);
 case 1:return sh2z_mat34_transform_points(w->out,&w->matrix,fl_source(w),w->count);
 case 2:if(!sh2z_mesh_view_init(&w->mesh,fl_source(w),w->count))return false;
  /* fall through */
 case 3:return sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&sh2z_transform_plan_execute(w->out,&w->plan);
 case 4:return sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&sh2z_transform_plan_execute_mac(w->out,&w->plan,0,w->count);
 case 5:return sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)&&sh2z_transform_plan_execute_dmuls(w->out,&w->plan,0,w->count);
 case 6:return sh2z_reuse_plan_update_matrix(&w->reuse_plan,&w->matrix)&&sh2z_reuse_plan_execute(w->out,&w->reuse_plan);
 case 7:return sh2z_transform_plan_execute(w->out,&w->plan);
 case 8: {
  if(!sh2z_transform_plan_init(&w->plan,&w->mesh,&w->matrix)||
     !sh2z_transform_plan_execute_mac(w->out,&w->plan,0,w->count))return false;
  const sh2z_vec3_t *p=fl_source(w);
  const uint32_t fc=(uint32_t)w->matrix.m[0][0],fs=(uint32_t)w->matrix.m[0][2];
  for(unsigned i=0;i<w->count;i++) {
   /* Restore the engine's separate product floors using only low halves.
    * Unsigned wrap preserves each product modulo 2^16 for all signs. */
   uint32_t ax=((uint32_t)p[i].x*fc)&65535u,az=((uint32_t)p[i].z*fs)&65535u;
   uint32_t bx=((uint32_t)p[i].x*fs)&65535u,bz=((uint32_t)p[i].z*fc)&65535u;
   w->out[i].x-=(int32_t)((ax+az)>>16);
   w->out[i].z+=(int32_t)(bz<bx);
  }
  return true;
 }
 default:return false;
 }
}
bool fl_correct(const furniture_lab *w,unsigned kernel) {
 const sh2z_vec3_t *expected=(kernel==0||kernel==8)?w->legacy:w->reference;
 for(unsigned i=0;i<w->count;i++)if(w->out[i].x!=expected[i].x||w->out[i].y!=expected[i].y||w->out[i].z!=expected[i].z)return false;
 return true;
}
uint32_t fl_hash(const sh2z_vec3_t *p,unsigned n) {
 uint32_t hash=2166136261u;
 for(unsigned i=0;i<n;i++){hash=(hash^(uint32_t)p[i].x)*16777619u;hash=(hash^(uint32_t)p[i].y)*16777619u;hash=(hash^(uint32_t)p[i].z)*16777619u;}
 return hash;
}

unsigned fl_raw_positions(unsigned model,sh2z_pos16_t *out) {
 if(model>=4||!out)return 0;
 for(unsigned b=0;b<sizes[model];b++)for(int i=0;i<8;i++) {
  sh2z_pos16_t *p=out+b*8+(unsigned)i;cbox_corner(models[model]+b,i,&p->x,&p->y,&p->z);
 }
 return sizes[model]*8;
}
int32_t fl_world_height(unsigned model){return model<4?heights[model]:0;}

static sh2z_mesh_view_t pose_meshes[4];
static volatile uint8_t pose_ready;
static volatile uint8_t *pose_ready_flag(void) {
#if defined(__sh__) && defined(__SH2__)
 return (volatile uint8_t *)((uintptr_t)&pose_ready|0x20000000u);
#else
 return &pose_ready;
#endif
}
bool fl_prepare_pose_meshes(void) {
 if(*pose_ready_flag())return true;
 for(unsigned m=0;m<4;m++)if(!sh2z_mesh_view_init(pose_meshes+m,fl_rom[m],sizes[m]*8))return false;
#if defined(__GNUC__)
 __asm__ volatile("":::"memory");
#endif
 *pose_ready_flag()=1;return true;
}
bool fl_pose_exact(sh2z_vec3_t *out,unsigned model,int32_t world_height,
 int32_t fc,int32_t fs,int32_t dx,int32_t dz) {
 if(!out||model>=4||heights[model]!=world_height||!*pose_ready_flag())return false;
 if(fc< -65536||fc>65536||fs< -65536||fs>65536||
    dx< -4194304||dx>4194304||dz< -4194304||dz>4194304)return false;
 const sh2z_mat34_t matrix={{{fc,0,fs,dx},{0,65536,0,0},{-fs,0,fc,dz}}};
 sh2z_transform_plan_t plan;
 if(!sh2z_transform_plan_init(&plan,pose_meshes+model,&matrix)||
    !sh2z_transform_plan_execute_mac(out,&plan,0,sizes[model]*8))return false;
 const sh2z_vec3_t *p=fl_rom[model];
 for(unsigned i=0;i<sizes[model]*8;i++) {
  uint32_t ax=((uint32_t)p[i].x*(uint32_t)fc)&65535u,az=((uint32_t)p[i].z*(uint32_t)fs)&65535u;
  uint32_t bx=((uint32_t)p[i].x*(uint32_t)fs)&65535u,bz=((uint32_t)p[i].z*(uint32_t)fc)&65535u;
  out[i].x-=(int32_t)((ax+az)>>16);out[i].z+=(int32_t)(bz<bx);
 }
 return true;
}
