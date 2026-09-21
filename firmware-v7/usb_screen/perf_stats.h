#ifndef CABADGE_PERF_STATS_H
#define CABADGE_PERF_STATS_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define PERF_SAMPLES 256
typedef struct {uint32_t us,bytes;} perf_sample_t;
typedef struct {perf_sample_t values[PERF_SAMPLES];unsigned count,total;uint64_t us,bytes;} perf_stats_t;
static inline void perf_add(perf_stats_t *s,uint32_t us,uint32_t bytes){
    s->total++;s->us+=us;s->bytes+=bytes;if(s->count<PERF_SAMPLES)s->values[s->count++]=(perf_sample_t){us,bytes};
}
static inline int perf_compare(const void *a,const void *b){uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return (x>y)-(x<y);}
static inline uint32_t perf_percentile(const perf_stats_t *s,unsigned percent,int bytes){
    if(!s->count||percent<1||percent>100)return 0;
    uint32_t sorted[PERF_SAMPLES];for(unsigned i=0;i<s->count;i++)sorted[i]=bytes?s->values[i].bytes:s->values[i].us;
    qsort(sorted,s->count,sizeof(*sorted),perf_compare);return sorted[(s->count*percent+99)/100-1];
}
#endif
