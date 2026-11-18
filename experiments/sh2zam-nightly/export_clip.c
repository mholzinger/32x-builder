#include "clip_lab.h"
#include "chair3d.h"
#include <stdio.h>
static clip_lab w;
int main(void) {
 puts("[");
 for(unsigned model=0;model<4;model++)for(unsigned mode=0;mode<2;mode++) {
  if(!cl_init(&w,model,37,192,11796,64)||!cl_run(&w,mode))return 1;
  printf("{\"model\":%u,\"mode\":%u,\"partial\":%u,\"polygons\":[",model,mode,w.partial);
  unsigned emitted=0;
  for(unsigned f=0;f<w.faces;f++)if(w.counts[f]>=3) {
   const sh2z_view_vertex_t *p=w.vertices+f*6;int64_t area=0;
   for(unsigned i=0;i<w.counts[f];i++){unsigned j=(i+1)%w.counts[f];area+=(int64_t)p[i].sx*p[j].sy-(int64_t)p[j].sx*p[i].sy;}
   if(area>=0)continue;
   printf("%s{\"clipped\":%s,\"points\":[",emitted++?",":"",w.counts[f]!=4?"true":"false");
   for(unsigned i=0;i<w.counts[f];i++)printf("%s[%d,%d]",i?",":"",p[i].sx,p[i].sy);
   printf("]}");
  }
  printf("]}%s\n",model==3&&mode==1?"":",");
 }
 puts("]");return 0;
}
