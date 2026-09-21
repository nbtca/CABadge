#include "perf_stats.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    perf_stats_t s={0};assert(!perf_percentile(&s,95,0));
    for(unsigned i=100;i;i--)perf_add(&s,i*100,i*2);
    assert(s.total==100&&s.us==505000&&s.bytes==10100);
    assert(perf_percentile(&s,50,0)==5000&&perf_percentile(&s,95,0)==9500&&perf_percentile(&s,95,1)==190);
    for(int i=0;i<200;i++)perf_add(&s,1,2);
    assert(s.total==300&&s.count==PERF_SAMPLES);puts("Probe percentile, totals, empty and overflow: PASS");return 0;
}
