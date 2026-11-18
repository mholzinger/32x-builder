#include "reuse_lab.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d\n",__LINE__);exit(1);}}while(0)
static reuse_lab w;
static uint32_t seed=12345;
static int32_t next(void){seed=seed*1664525u+1013904223u;return (int32_t)seed;}
static int32_t oracle(const int32_t *r,sh2z_vec3_t v) {
 int64_t raw=(int64_t)r[0]*v.x+(int64_t)r[1]*v.y+(int64_t)r[2]*v.z;
 int64_t q=raw/65536-(raw<0&&raw%65536!=0)+r[3];return q>INT32_MAX?INT32_MAX:q<INT32_MIN?INT32_MIN:(int32_t)q;
}
int main(void) {
 unsigned checked=0;
 for(unsigned f=0;f<4;f++) {
  CHECK(rl_init(&w,f));
  printf("fixture=%u count=%u x=%zu yz=%zu reusable=%u blocks8=%u hash=%08x workspace=%zu\n",f,w.count,w.reuse.x_count,w.reuse.yz_count,w.reuse_plan.enabled?1u:0u,w.reuse.blocks8?1u:0u,(unsigned)fl_hash(w.reference,w.count),sizeof w);
  for(unsigned k=0;k<RL_KERNELS;k++)CHECK(rl_run(&w,k)&&rl_correct(&w));
  for(unsigned n=0;n<32;n++) {
   for(unsigned r=0;r<3;r++)for(unsigned c=0;c<4;c++)w.matrix.m[r][c]=next()/4096;
   for(unsigned i=0;i<w.count;i++)w.reference[i]=(sh2z_vec3_t){oracle(w.matrix.m[0],w.in[i]),oracle(w.matrix.m[1],w.in[i]),oracle(w.matrix.m[2],w.in[i])};
   for(unsigned k=0;k<RL_KERNELS;k++) {
    if(k==5)continue; /* execute-only uses the already bound matrix */
    CHECK(rl_run(&w,k)&&rl_correct(&w));checked+=w.count;
   }
  }
 }
 printf("random-matrix vertex-oracles=%u\n",checked);return 0;
}
