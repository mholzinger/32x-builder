/* Standalone math microbench. Secondary stays in BIOS; not a gameplay FPS test. */
#include "mars.h"
#include "font.h"
#include "box_hero.h"
#include "sh2zam_bench.h"
#include "../third_party/SH2ZAM/benchmarks/small-batch/small_batch.h"
/* Exposed for emulator dumps. Native-endian words; avg is ticks * 100. */
struct bench_result {uint32_t avg100,min,max,hash,fail;};
volatile struct bench_result sh2zam_bench_results[9];
typedef char benchmark_workspace_must_fit_title_scratch[(sizeof(sb_workspace)<=71680)?1:-1];
static uint16_t timer(void) {uint8_t hi=SH2_FRT_FRCH;uint8_t lo=SH2_FRT_FRCL;return ((uint16_t)hi<<8)|lo;}
static uint32_t div_run(unsigned kind) {
    uint32_t sum=0;
    for(unsigned i=0;i<32;i++) {
        int32_t a=1234567+(int32_t)i*771,b=98765+(int32_t)i*13;
        int32_t q;sh2z_divu_t t;
        if(kind==0)q=sh2z_div(a,b);
        else if(kind==1)q=sh2z_divu_div16(a,b,0);
        else if(kind==2)sh2z_divu_start16(&t,a,b);
        else q=sh2z_divu_div64((int64_t)a*65536,b,0);
        /* Identical useful independent work in every variant. */
        uint32_t work=(i+sum)*1664525u+1013904223u;
        work^=work>>13;work*=16777619u;
        if(kind==2)q=sh2z_divu_finish(&t,0);
        sum=work^(uint32_t)q;
    }
    return sum;
}
static void number(uint8_t *fb,int x,int y,uint32_t value) {
    char text[6]="00000";
    for(int d=4;d>=0;d--){text[d]='0'+value%10;value/=10;}
    font_draw_string(fb,x,y,text,49);
}
void sh2zam_benchmark(unsigned page) {
    static const unsigned counts[]={8,24,28,48,72,128,256};
    static const char *const div_names[]={"DIV1","DIVU16","SPLIT16","DIVU64"};
    sb_workspace *w=(sb_workspace *)(void *)hero_scratch;
    unsigned rows=page==8?4:9;
    unsigned fail=0;uint32_t hash=0;
    Hw32xInit(MARS_VDP_MODE_256,0);
    Hw32xSetBGColor(0,0,0,0);Hw32xSetBGColor(49,31,31,31);
    SH2_FRT_TIER=1;SH2_FRT_TCR=2;SH2_FRT_FTCSR=0;
    if(page!=8) {if(!sb_init(w,counts[page-1]))fail++;
        hash=sb_hash(w->reference,w->count);}
    else {fail=sb_check_divu();hash=div_run(0);}
    for(unsigned k=0;k<rows;k++) {
        uint32_t sum=0,minimum=65535,maximum=0,bad=0,h=0;
        for(unsigned n=0;n<72;n++) {
            uint16_t start=timer();
            int ok=1;
            if(page==8)h=div_run(k);else ok=sb_run(w,k);
            uint16_t dt=(uint16_t)(timer()-start);
            if(page!=8) {h=sb_hash(w->out,w->count);if(!sb_correct(w))bad++;}
            if(!ok||h!=hash)bad++;
            if(n>=8) {sum+=dt;if(dt<minimum)minimum=dt;if(dt>maximum)maximum=dt;}
        }
        sh2zam_bench_results[k]=(struct bench_result){sum*100/64,minimum,maximum,h,bad};
        fail+=bad;
    }
    for(;;) {
        uint8_t *fb=(uint8_t *)&MARS_FRAMEBUFFER+0x200;
        for(unsigned i=0;i<320*224;i++)fb[i]=0;
        font_draw_string(fb,8,12,page==8?"SH2ZAM DIVU N:":"SH2ZAM BATCH N:",49);
        number(fb,128,12,page==8?32:counts[page-1]);
        font_draw_string(fb,8,28,"FAIL:",49);
        font_draw_string(fb,104,28,"REUSE:",49);
        font_draw_string(fb,208,28,"MAC:",49);
        number(fb,48,28,fail);
        number(fb,152,28,page==8?0:w->reuse_plan.enabled);
        number(fb,240,28,page==8?0:sh2z_transform_plan_mac_available(&w->plan));
        char hex[9];for(unsigned i=0;i<8;i++)hex[i]="0123456789ABCDEF"[(hash>>(28-i*4))&15];hex[8]=0;
        font_draw_string(fb,8,40,"HASH:",49);font_draw_string(fb,48,40,hex,49);
        font_draw_string(fb,8,54,"KERNEL       AVG    MIN    MAX",49);
        for(unsigned k=0;k<rows;k++) {
            const volatile struct bench_result *res=&sh2zam_bench_results[k];
            font_draw_string(fb,8,68+(int)k*12,page==8?div_names[k]:sb_names[k],49);
            number(fb,112,68+(int)k*12,res->avg100/100);
            number(fb,168,68+(int)k*12,res->min);
            number(fb,224,68+(int)k*12,res->max);
        }
        font_draw_string(fb,8,190,"TICKS PHI/128 / CALL",49);
        font_draw_string(fb,8,202,"CPU2 PARKED - NOT GAME FPS",49);
        Hw32xScreenFlip(1);
    }
}
