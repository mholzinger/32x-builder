/* Clipping experiment, retains polygons for geometric audit, not shipping fills. */
#include "clip_lab.h"
#include "chair3d.h"
bool cl_init(clip_lab *w,unsigned model,unsigned facing,unsigned yaw,int32_t distance,unsigned eye) {
 if(!w||!vl_init(&w->scene,model,facing,yaw,distance,eye))return false;
 w->faces=w->scene.count/8*6;return true;
}
bool cl_run(clip_lab *w,unsigned kernel) {
 if(kernel>2)return false;
 w->emitted=0;w->partial=0;
 for(unsigned f=0;f<w->faces;f++) {
  unsigned b=f/6,side=f%6,inside=0;
  sh2z_view_vertex_t input[4];
  for(unsigned i=0;i<4;i++){input[i]=w->scene.reference[b*8+chair_face_v[side][i]];inside+=input[i].z>=w->scene.view.near_z;}
  if(inside>0&&inside<4)w->partial++;
  sh2z_view_vertex_t *out=w->vertices+f*6;
  unsigned n;
  if(kernel==0){n=inside==4?4:0;for(unsigned i=0;i<n;i++)out[i]=input[i];}
  else n=kernel==1?sh2z_view_clip_near(out,&w->scene.view,input):sh2z_view_clip_near_divu(out,&w->scene.view,input);
  w->counts[f]=n;w->emitted+=n;
 }
 return true;
}
uint32_t cl_hash(const clip_lab *w) {
 uint32_t hash=2166136261u;
 for(unsigned f=0;f<w->faces;f++){hash=(hash^w->counts[f])*16777619u;hash=(hash^vl_hash(w->vertices+f*6,w->counts[f]))*16777619u;}
 return hash;
}
