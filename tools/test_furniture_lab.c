#include "furniture_lab.h"
#include "chair3d.h"
#include "desk3d.h"
#include "pvm3d.h"
#include "desk_pvm3d.h"
#include "sin_table.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d\n",__LINE__);exit(1);}}while(0)
static furniture_lab w;
static int64_t floor16(int64_t v){int64_t q=v/65536;return q-(v<0&&v%65536!=0);}
int main(void) {
 const cbox_t *models[]={chair_boxes,desk_boxes,pvm_boxes,desk_pvm_boxes};
 const int32_t heights[]={24576,20316,26214,(26214*294/256)};
 CHECK(fl_prepare_pose_meshes());
 unsigned changed=0,max_delta=0,vertices=0;
 for(unsigned model=0;model<4;model++)for(unsigned facing=0;facing<256;facing++)for(unsigned rom=0;rom<2;rom++) {
  CHECK(fl_init(&w,model,rom!=0,facing));
  for(unsigned i=0;i<w.count;i++) {
   int16_t x,y,z;cbox_corner(models[model]+i/8,(int)(i%8),&x,&y,&z);
   int32_t h=heights[model];
   int64_t wx=(int64_t)x*h,wy=(int64_t)y*h,wz=(int64_t)z*h;
   wx=wx/256-(wx<0&&wx%256!=0);wy=wy/256-(wy<0&&wy%256!=0);wz=wz/256-(wz<0&&wz%256!=0);
   CHECK(fl_source(&w)[i].x==wx&&fl_source(&w)[i].y==wy&&fl_source(&w)[i].z==wz);
   int64_t fc=COS_FX(facing+64),fs=-SIN_FX(facing+64);
   int64_t ex=floor16(wx*fc)+floor16(wz*fs)+12345,ez=-floor16(wx*fs)+floor16(wz*fc)+98304;
   CHECK(w.legacy[i].x==ex&&w.legacy[i].y==wy&&w.legacy[i].z==ez);
   int64_t ox=floor16(wx*fc+wz*fs)+12345,oz=floor16(-wx*fs+wz*fc)+98304;
   CHECK(w.reference[i].x==ox&&w.reference[i].z==oz);
   unsigned dx=(unsigned)llabs(ex-ox),dz=(unsigned)llabs(ez-oz);
   if(dx||dz)changed++;if(dx>max_delta)max_delta=dx;if(dz>max_delta)max_delta=dz;
   CHECK(dx<=1&&dz<=1);vertices++;
  }
  for(unsigned k=0;k<FL_KERNELS;k++)CHECK(fl_run(&w,k)&&fl_correct(&w,k));
  CHECK(fl_pose_exact(w.out,model,heights[model],COS_FX(facing+64),-SIN_FX(facing+64),12345,98304));
  CHECK(fl_correct(&w,8));
  CHECK(!fl_pose_exact(w.out,model,heights[model]+1,COS_FX(facing+64),-SIN_FX(facing+64),12345,98304));
 }
 printf("actual mesh vertices=%u changed-by-single-floor=%u max-world-ulp=%u workspace=%zu\n",vertices,changed,max_delta,sizeof w);
 return 0;
}
