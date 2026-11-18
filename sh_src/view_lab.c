/* Fused view adapter with raw 8.8 coordinates treated as whole model ticks.
 * All camera distances/near plane are converted to the SAME model-tick unit.
 * No input fractions are dropped; numeric compatibility is measured separately. */
#include "view_lab.h"
#include "sin_table.h"
#include <sh2zam/divu_experimental.h>
static int32_t floor_shift(int64_t n,unsigned bits){int64_t d=(int64_t)1<<bits;return (int32_t)(n/d-(n<0&&n%d!=0));}
static int32_t mul(int32_t a,int32_t b){return floor_shift((int64_t)a*b,16);}
static int32_t divide(int32_t a,int32_t b) {
#if defined(__sh__) && defined(__SH2__)
 int32_t q,hi=a>>16;uint32_t lo=(uint32_t)a<<16;
 __asm__ volatile("mov #-128,r1\n\tadd r1,r1\n\tmov.l %1,@(0,r1)\n\tmov.l %2,@(16,r1)\n\tmov.l %3,@(20,r1)\n\tmov.l @(20,r1),%0":"=r"(q):"r"(b),"r"(hi),"r"(lo):"r1");
 return q;
#else
 return (int32_t)(((int64_t)a*65536)/b);
#endif
}
static int32_t ticks14(int32_t world,const vl_camera *c){return (int32_t)((int64_t)world*4194304/c->height);}
static int16_t q14(int32_t n){return (int16_t)(n>=0?(n+2)/4:-((-n+2)/4));}
bool vl_setup_view(sh2z_view_t *v,const vl_camera *c) {
 sh2z_view_t r={{{0}},{0},160*65536,224*65536,160,112,1,2,{0,0,319,223}};
 r.r[0][0]=q14(mul(c->invdet,mul(c->diry,c->fc)+mul(c->dirx,c->fs)));
 r.r[0][2]=q14(mul(c->invdet,mul(c->diry,c->fs)-mul(c->dirx,c->fc)));
 r.r[1][1]=16384;
 r.r[2][0]=q14(mul(c->invdet,-mul(c->planey,c->fc)-mul(c->planex,c->fs)));
 r.r[2][2]=q14(mul(c->invdet,-mul(c->planey,c->fs)+mul(c->planex,c->fc)));
 r.t[0]=ticks14(mul(c->invdet,mul(c->diry,-c->px)-mul(c->dirx,-c->py)),c);
 r.t[1]=ticks14(-(int32_t)c->eye*256,c);
 r.t[2]=ticks14(mul(c->invdet,-mul(c->planey,-c->px)+mul(c->planex,-c->py)),c);
 r.near_z=ticks14(c->nearz,c);r.far_z=ticks14(16*65536,c);
 return sh2z_view_init(v,&r);
}
void vl_engine(sh2z_view_vertex_t *out,const sh2z_pos16_t *raw,unsigned n,const vl_camera *c) {
 for(unsigned i=0;i<n;i++) {
  int32_t wx=floor_shift((int64_t)raw[i].x*c->height,8),wy=floor_shift((int64_t)raw[i].y*c->height,8),wz=floor_shift((int64_t)raw[i].z*c->height,8);
  int32_t ddx=mul(wx,c->fc)+mul(wz,c->fs)-c->px;
  int32_t ddy=-mul(wx,c->fs)+mul(wz,c->fc)-c->py;
  int32_t depth=mul(c->invdet,-mul(c->planey,ddx)+mul(c->planex,ddy));
  int32_t lateral=mul(c->invdet,mul(c->diry,ddx)-mul(c->dirx,ddy));
  sh2z_view_vertex_t p={lateral,wy,depth,0,0,0,{0,0,0}};
  if(depth<c->nearz)p.outcode=SH2Z_OUT_NEAR;
  else {
   int32_t inv=divide(65536,depth);
   int32_t sx=160+floor_shift((int64_t)160*mul(lateral,inv),16);
   int32_t sy=112+floor_shift((int64_t)((224*(int)c->eye)>>8)*inv,16)-floor_shift((int64_t)mul(wy,inv)*224,16);
   p.sx=(int16_t)(sx<INT16_MIN?INT16_MIN:sx>INT16_MAX?INT16_MAX:sx);
   p.sy=(int16_t)(sy<INT16_MIN?INT16_MIN:sy>INT16_MAX?INT16_MAX:sy);
   if(depth>16*65536)p.outcode|=SH2Z_OUT_FAR;
   if(sx<0)p.outcode|=SH2Z_OUT_LEFT;if(sx>319)p.outcode|=SH2Z_OUT_RIGHT;
   if(sy<0)p.outcode|=SH2Z_OUT_TOP;if(sy>223)p.outcode|=SH2Z_OUT_BOTTOM;
  }
  out[i]=p;
 }
}
bool vl_init(view_lab *w,unsigned model,unsigned facing,unsigned yaw,int32_t distance,unsigned eye) {
 if(!w||model>=4||eye>256)return false;
 w->model=model;w->count=fl_raw_positions(model,w->raw);
 vl_camera c;
 c.dirx=COS_FX(yaw);c.diry=SIN_FX(yaw);c.planex=mul(-c.diry,43253);c.planey=mul(c.dirx,43253);
 int32_t det=mul(c.planex,c.diry)-mul(c.dirx,c.planey);if(!det)return false;
 c.invdet=divide(65536,det);c.fc=COS_FX(facing+64);c.fs=-SIN_FX(facing+64);
 c.px=-mul(distance,c.dirx);c.py=-mul(distance,c.diry);c.eye=eye;c.nearz=5242;c.height=fl_world_height(model);
 w->camera=c;
 if(!vl_setup_view(&w->view,&c))return false;
 vl_engine(w->legacy,w->raw,w->count,&c);
 return sh2z_view_batch(w->reference,&w->view,w->raw,sizeof(*w->raw),w->count);
}
bool vl_run(view_lab *w,unsigned kernel) {
 if(kernel==0){vl_engine(w->out,w->raw,w->count,&w->camera);return true;}
 if(kernel<3&&!vl_setup_view(&w->view,&w->camera))return false;
 if(kernel==1)return sh2z_view_batch(w->out,&w->view,w->raw,sizeof(*w->raw),w->count);
 if(kernel==2||kernel==3)return sh2z_view_batch_divu(w->out,&w->view,w->raw,sizeof(*w->raw),w->count);
 return false;
}
uint32_t vl_hash(const sh2z_view_vertex_t *p,unsigned n) {
 uint32_t hash=2166136261u;
 for(unsigned i=0;i<n;i++) {
  hash=(hash^(uint32_t)p[i].x)*16777619u;hash=(hash^(uint32_t)p[i].y)*16777619u;hash=(hash^(uint32_t)p[i].z)*16777619u;
  hash=(hash^(uint16_t)p[i].sx)*16777619u;hash=(hash^(uint16_t)p[i].sy)*16777619u;hash=(hash^p[i].outcode)*16777619u;
 }
 return hash;
}
