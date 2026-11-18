#ifndef SH2ZAM_BENCH_H
#define SH2ZAM_BENCH_H
#include <stdint.h>
struct bench_result {uint32_t avg100,min,max,hash,fail;};
extern volatile struct bench_result sh2zam_bench_results[9];
void sh2zam_benchmark(unsigned page);
void furniture_lab_benchmark(unsigned page);
void view_lab_benchmark(unsigned page);
void reuse_lab_benchmark(unsigned page);
#endif
