/* Diagnostic-only two real SH-2 consumers. No audio/normal renderer dispatch.
 * Both workspaces and the mailbox occupy title scratch, not persistent BSS. */
#include "mars.h"
#include "shared.h"
#include "sh2_asm.h"
#include "font.h"
#include "box_hero.h"
#include "sh2zam_bench.h"
#include "furniture_lab.h"
#include "shared_geometry_lab.h"
typedef struct {volatile uint32_t ready,request,done,mode,facing,published,hash;} mailbox;
#define PRIMARY ((furniture_lab *)(void *)hero_scratch)
#define SECONDARY ((furniture_lab *)(void *)(hero_scratch+16384))
#define MAIL ((mailbox *)((uintptr_t)(hero_scratch+32768)|0x20000000u))
typedef char sg_primary_fits[(sizeof(furniture_lab)<16384)?1:-1];
typedef char sg_secondary_fits[(sizeof(furniture_lab)<16384)?1:-1];
static uint16_t timer(void){uint8_t h=SH2_FRT_FRCH;uint8_t l=SH2_FRT_FRCL;return ((uint16_t)h<<8)|l;}
static void barrier(void){__asm__ volatile("":::"memory");}
static void backoff(void){uint16_t start=timer();while((uint16_t)(timer()-start)<6){}}
void shared_geometry_lab_slave(unsigned page) {
 SH2_FRT_TIER=1;SH2_FRT_TCR=2;SH2_FRT_FTCSR=0;
 furniture_lab *w=SECONDARY;fl_init(w,page==25?0:3,false,0);
 mailbox *m=MAIL;uint32_t seen=0;barrier();m->ready=1;
 for(;;) {
  uint32_t sequence=m->request;if(sequence==seen){backoff();continue;}
  unsigned mode=m->mode;w->facing=m->facing;
  uint32_t hash;
  if(mode==0){fl_run(w,8);hash=fl_hash(w->out,w->count);}
  else {
   while(m->published!=sequence)backoff();barrier();
   sh2z_vec3_t *source=PRIMARY->out;
   if(mode==1)source=(sh2z_vec3_t *)((uintptr_t)source|0x20000000u);
   if(mode==2) {
    uintptr_t start=(uintptr_t)source&~(uintptr_t)15;
    uint32_t bytes=(uint32_t)(w->count*sizeof(*source)+(uintptr_t)source-start);
    Mars_ClearCacheLines((void *)start,(bytes+15)/16);
   }
   hash=fl_hash(source,w->count);
  }
  m->hash=hash;barrier();m->done=sequence;seen=sequence;
 }
}
static void number(uint8_t *fb,int x,int y,uint32_t v){char s[6]="00000";for(int i=4;i>=0;i--){s[i]='0'+v%10;v/=10;}font_draw_string(fb,x,y,s,49);}
void shared_geometry_lab_master(unsigned page) {
 static const char *const names[]={"DUPLICATE","SHARED-UC","SHARED-INV","STALE-PROBE"};
 furniture_lab *w=PRIMARY;mailbox *m=MAIL;uint32_t sequence=0;unsigned fail=0;
 Hw32xInit(MARS_VDP_MODE_256,0);Hw32xSetBGColor(0,0,0,0);Hw32xSetBGColor(49,31,31,31);
 SH2_FRT_TIER=1;SH2_FRT_TCR=2;SH2_FRT_FTCSR=0;
 m->ready=m->request=m->done=m->published=0;fl_init(w,page==25?0:3,false,0);
 barrier();MARS_SYS_COMM4=0;
 uint16_t ready_start=timer();while(!m->ready)if((uint16_t)(timer()-ready_start)>60000){fail++;break;}
 for(unsigned mode=0;mode<4;mode++) {
  uint32_t sum=0,minimum=65535,maximum=0,bad=0,hash=0;
  for(unsigned n=0;n<72;n++) {
   unsigned facing=((n+1u)*37u)&255u;sequence++;w->facing=facing;
   m->mode=mode;m->facing=facing;barrier();
   uint16_t start=timer();m->request=sequence;
   fl_run(w,8);barrier();m->published=sequence;
   hash=fl_hash(w->out,w->count);
   while(m->done!=sequence) {
    if((uint16_t)(timer()-start)>60000){fail++;break;}
   }
   uint16_t dt=(uint16_t)(timer()-start);
   uint32_t remote=m->hash;
   fl_run(w,0);uint32_t oracle=fl_hash(w->out,w->count);
   if(hash!=oracle||remote!=oracle)bad++;
   if(n>=8){sum+=dt;if(dt<minimum)minimum=dt;if(dt>maximum)maximum=dt;}
  }
  sh2zam_bench_results[mode]=(struct bench_result){sum*100/64,minimum,maximum,hash,bad};
  if(mode<3)fail+=bad;else if(!bad)fail++; /* negative coherency control must fail */
 }
 for(;;) {
  uint8_t *fb=(uint8_t *)&MARS_FRAMEBUFFER+0x200;for(unsigned i=0;i<320*224;i++)fb[i]=0;
  font_draw_string(fb,8,12,"SH2ZAM DUAL N:",49);number(fb,128,12,w->count);
  font_draw_string(fb,8,28,"FAIL:",49);number(fb,48,28,fail);
  font_draw_string(fb,104,28,"REUSE:",49);number(fb,152,28,0);
  font_draw_string(fb,208,28,"MAC:",49);number(fb,240,28,sh2z_transform_plan_mac_available(&w->plan));
  font_draw_string(fb,8,40,"SIX TICK POLL / REAL TWO CPU",49);
  font_draw_string(fb,8,54,"KERNEL       AVG    MIN    MAX",49);
  for(unsigned k=0;k<4;k++) {
   const volatile struct bench_result *res=&sh2zam_bench_results[k];
   font_draw_string(fb,8,68+(int)k*12,names[k],49);
   number(fb,112,68+(int)k*12,res->avg100/100);number(fb,168,68+(int)k*12,res->min);number(fb,224,68+(int)k*12,res->max);
  }
  font_draw_string(fb,8,134,"STALE FAILS:",49);number(fb,112,134,sh2zam_bench_results[3].fail);
  font_draw_string(fb,8,190,"TICKS PHI/128 / BOTH DONE",49);
  font_draw_string(fb,8,202,"NO AUDIO - NOT GAME FPS",49);Hw32xScreenFlip(1);
 }
}
