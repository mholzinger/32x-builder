#include "view_lab.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d\n",__LINE__);exit(1);}}while(0)
static view_lab w;
int main(void) {
 const int32_t distances[]={3932,5242,7864,19660,49152,98304,229376};
 unsigned long total=0,near_mismatch=0,visible=0,pixel_mismatch=0,onscreen_mismatch=0;
 unsigned maxdx=0,maxdy=0,max_in_dx=0,max_in_dy=0;
 for(unsigned m=0;m<4;m++)for(unsigned f=0;f<256;f+=16)for(unsigned yaw=0;yaw<256;yaw+=4)for(unsigned d=0;d<7;d++)for(unsigned e=64;e<=128;e+=64) {
  CHECK(vl_init(&w,m,f,yaw,distances[d],e));
  CHECK(vl_run(&w,2));
  for(unsigned i=0;i<w.count;i++) {
   const sh2z_view_vertex_t *a=w.out+i,*b=w.reference+i;
   CHECK(a->x==b->x&&a->y==b->y&&a->z==b->z&&a->sx==b->sx&&a->sy==b->sy&&a->outcode==b->outcode);
  }
  for(unsigned i=0;i<w.count;i++) {
   total++;const sh2z_view_vertex_t *a=w.legacy+i,*b=w.reference+i;
   if((a->outcode&1)!=(b->outcode&1)){near_mismatch++;continue;}
   if(a->outcode&1)continue;
   visible++;unsigned dx=(unsigned)abs((int)a->sx-b->sx),dy=(unsigned)abs((int)a->sy-b->sy);
   if(dx||dy)pixel_mismatch++;if(dx>maxdx)maxdx=dx;if(dy>maxdy)maxdy=dy;
   if(a->sx>=0&&a->sx<320&&a->sy>=0&&a->sy<224) {
    if(dx||dy)onscreen_mismatch++;if(dx>max_in_dx)max_in_dx=dx;if(dy>max_in_dy)max_in_dy=dy;
   }
  }
 }
 printf("vertices=%lu near_mismatch=%lu in_front=%lu pixel_mismatch=%lu onscreen_mismatch=%lu max_dx=%u max_dy=%u max_onscreen_dx=%u max_onscreen_dy=%u workspace=%zu\n",total,near_mismatch,visible,pixel_mismatch,onscreen_mismatch,maxdx,maxdy,max_in_dx,max_in_dy,sizeof w);
 return 0;
}
