/* Correctness/fixture manifest only. Host speed is not SH-2 performance. MIT. */
#include "small_batch.h"
#include <stdio.h>
static sb_workspace w;
int main(void) {
    const unsigned counts[]={8,24,28,48,72,128,256};
    for(unsigned c=0;c<7;c++) {
        if(!sb_init(&w,counts[c]))return 1;
        for(unsigned k=0;k<SB_KERNELS;k++)if(!sb_run(&w,k)||!sb_correct(&w))return 2;
        printf("count=%u hash=%08x reuse=%u blocks8=%u workspace=%zu\n",
            counts[c],(unsigned)sb_hash(w.reference,w.count),w.reuse_plan.enabled?1u:0u,
            w.reuse.blocks8?1u:0u,sizeof w);
    }
    return sb_check_divu()?3:0;
}
