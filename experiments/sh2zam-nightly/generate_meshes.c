/* Local fixture generator: expands the project's actual box/wedge models. */
#include <stdio.h>
#include "chair3d.h"
#include "desk3d.h"
#include "pvm3d.h"
#include "desk_pvm3d.h"
static int floor8(int n){return n>=0?n/256:-1-((-1-n)/256);}
int main(void) {
 const cbox_t *models[]={chair_boxes,desk_boxes,pvm_boxes,desk_pvm_boxes};
 const unsigned sizes[]={CHAIR_NBOXES,DESK_NBOXES,PVM_NBOXES,DESK_PVM_NBOXES};
 const int heights[]={24576,20316,26214,(26214*294/256)};
 puts("/* Generated from actual box/wedge headers. Local fixtures, no upstream asset redistribution. */");
 for(unsigned m=0;m<4;m++) {
  printf("static const sh2z_vec3_t fl_rom_%u[%u]={\n",m,sizes[m]*8);
  for(unsigned b=0;b<sizes[m];b++)for(int v=0;v<8;v++) {
   int16_t x,y,z;cbox_corner(models[m]+b,v,&x,&y,&z);
   printf("{%d,%d,%d},\n",floor8(x*heights[m]),floor8(y*heights[m]),floor8(z*heights[m]));
  }
  puts("};");
 }
 puts("static const sh2z_vec3_t *const fl_rom[]={fl_rom_0,fl_rom_1,fl_rom_2,fl_rom_3};");
 return 0;
}
