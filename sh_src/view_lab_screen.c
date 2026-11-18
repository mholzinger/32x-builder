#include "mars.h"
#include "font.h"
#include "box_hero.h"
#include "sh2zam_bench.h"
#include "clip_lab.h"
typedef char vl_fits_title_scratch[(sizeof(clip_lab)<=71680)?1:-1];
static uint16_t timer(void){uint8_t hi=SH2_FRT_FRCH;uint8_t lo=SH2_FRT_FRCL;return ((uint16_t)hi<<8)|lo;}
static void number(uint8_t *fb,int x,int y,uint32_t v){char s[6]="00000";for(int i=4;i>=0;i--){s[i]='0'+v%10;v/=10;}font_draw_string(fb,x,y,s,49);}
void view_lab_benchmark(unsigned page) {
 static const char *const views[]={"ENGINE","VIEW-C","VIEW-HW","EXEC-ONLY"};
 static const char *const clips[]={"DROP","CLIP-C","CLIP-HW"};
 unsigned clipping=page>=21,model=(page-17)%4,rows=clipping?3:4,fail=0;
 clip_lab *clip=(clip_lab *)(void *)hero_scratch;view_lab *w=&clip->scene;
 uint32_t expected[4]={0};
 Hw32xInit(MARS_VDP_MODE_256,0);Hw32xSetBGColor(0,0,0,0);Hw32xSetBGColor(49,31,31,31);
 SH2_FRT_TIER=1;SH2_FRT_TCR=2;SH2_FRT_FTCSR=0;
 if(clipping) {
  if(!cl_init(clip,model,37,192,5242,64))fail++;
  cl_run(clip,0);expected[0]=cl_hash(clip);cl_run(clip,1);expected[1]=expected[2]=cl_hash(clip);
 } else {
  if(!vl_init(w,model,37,192,98304,128))fail++;
  expected[0]=vl_hash(w->legacy,w->count);
  expected[1]=expected[2]=expected[3]=vl_hash(w->reference,w->count);
 }
 for(unsigned k=0;k<rows;k++) {
  uint32_t sum=0,minimum=65535,maximum=0,bad=0,hash=0;
  for(unsigned n=0;n<72;n++) {
   uint16_t start=timer();bool ok=clipping?cl_run(clip,k):vl_run(w,k);uint16_t dt=(uint16_t)(timer()-start);
   hash=clipping?cl_hash(clip):vl_hash(w->out,w->count);
   if(!ok||hash!=expected[k])bad++;
   if(n>=8){sum+=dt;if(dt<minimum)minimum=dt;if(dt>maximum)maximum=dt;}
  }
  sh2zam_bench_results[k]=(struct bench_result){sum*100/64,minimum,maximum,hash,bad};fail+=bad;
 }
 uint32_t hash=expected[1];
 for(;;) {
  uint8_t *fb=(uint8_t *)&MARS_FRAMEBUFFER+0x200;for(unsigned i=0;i<320*224;i++)fb[i]=0;
  font_draw_string(fb,8,12,clipping?"SH2ZAM CLIP N:":"SH2ZAM VIEW N:",49);number(fb,128,12,w->count);
  font_draw_string(fb,8,28,"FAIL:",49);number(fb,48,28,fail);
  font_draw_string(fb,104,28,"REUSE:",49);number(fb,152,28,0);
  font_draw_string(fb,208,28,"MAC:",49);number(fb,240,28,0);
  char hex[9];for(unsigned i=0;i<8;i++)hex[i]="0123456789ABCDEF"[(hash>>(28-i*4))&15];hex[8]=0;
  font_draw_string(fb,8,40,"HASH:",49);font_draw_string(fb,48,40,hex,49);
  font_draw_string(fb,8,54,"KERNEL       AVG    MIN    MAX",49);
  for(unsigned k=0;k<rows;k++) {
   const volatile struct bench_result *res=&sh2zam_bench_results[k];
   font_draw_string(fb,8,68+(int)k*12,clipping?clips[k]:views[k],49);
   number(fb,112,68+(int)k*12,res->avg100/100);number(fb,168,68+(int)k*12,res->min);number(fb,224,68+(int)k*12,res->max);
  }
  if(clipping){font_draw_string(fb,8,134,"PARTIAL:",49);number(fb,80,134,clip->partial);font_draw_string(fb,8,146,"EMITTED:",49);number(fb,80,146,clip->emitted);}
  font_draw_string(fb,8,190,"TICKS PHI/128 / CALL",49);
  font_draw_string(fb,8,202,"CPU2 PARKED - NOT GAME FPS",49);Hw32xScreenFlip(1);
 }
}
