#include "clip_lab.h"
#include "chair3d.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d\n",__LINE__);exit(1);}}while(0)
static clip_lab w;
static sh2z_view_vertex_t saved[CL_MAX_FACES*6];
static unsigned counts[CL_MAX_FACES];
static int same(sh2z_view_vertex_t a,sh2z_view_vertex_t b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.sx==b.sx&&a.sy==b.sy&&a.outcode==b.outcode;}
int main(void) {
 const int32_t distances[]={-19660,0,3932,5242,7864,19660,98304};
 unsigned long faces=0,partial=0,recovered=0,intersections=0;
 for(unsigned m=0;m<4;m++)for(unsigned f=0;f<256;f+=16)for(unsigned yaw=0;yaw<256;yaw+=8)for(unsigned d=0;d<7;d++)for(unsigned eye=64;eye<=128;eye+=64) {
  CHECK(cl_init(&w,m,f,yaw,distances[d],eye));CHECK(cl_run(&w,1));
  faces+=w.faces;partial+=w.partial;
  for(unsigned face=0;face<w.faces;face++) {
   sh2z_view_vertex_t input[4],expected[6];unsigned n=0;
   for(unsigned i=0;i<4;i++)input[i]=w.scene.reference[(face/6)*8+chair_face_v[face%6][i]];
   for(unsigned i=0;i<4;i++) {
    sh2z_view_vertex_t a=input[i],b=input[(i+1)%4];int ai=a.z>=w.scene.view.near_z,bi=b.z>=w.scene.view.near_z;
    if(ai)expected[n++]=a;
    if(ai!=bi) {
     sh2z_view_vertex_t in=ai?a:b,out=ai?b:a,p={0};
     int64_t num=(int64_t)in.z-w.scene.view.near_z,den=(int64_t)in.z-out.z;
     p.x=(int32_t)(in.x+((int64_t)out.x-in.x)*num/den);
     p.y=(int32_t)(in.y+((int64_t)out.y-in.y)*num/den);p.z=w.scene.view.near_z;
     CHECK(sh2z_view_project(&p,&w.scene.view,&p,1));expected[n++]=p;intersections++;
    }
   }
   CHECK(n==w.counts[face]);CHECK(n<=5);counts[face]=n;
   for(unsigned i=0;i<n;i++){CHECK(same(expected[i],w.vertices[face*6+i]));CHECK(w.vertices[face*6+i].z>=w.scene.view.near_z);saved[face*6+i]=w.vertices[face*6+i];}
  }
  CHECK(cl_run(&w,2));
  for(unsigned face=0;face<w.faces;face++){CHECK(w.counts[face]==counts[face]);for(unsigned i=0;i<counts[face];i++)CHECK(same(saved[face*6+i],w.vertices[face*6+i]));}
  unsigned clipped=w.emitted;CHECK(cl_run(&w,0));if(clipped>w.emitted)recovered+=clipped-w.emitted;
 }
 printf("faces=%lu partially-crossing=%lu intersection-oracles=%lu recovered-vertices=%lu workspace=%zu\n",faces,partial,intersections,recovered,sizeof w);
 return 0;
}
