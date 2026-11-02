#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Admission reserves only resources not already owned by lane 1. The 10 KiB
 * task allowance includes the 8 KiB stack/TCB/queues; 14 KiB covers client and
 * transient internal transport allocations. Decode and HTTP chunks stay PSRAM.
 * Runtime shedding uses the remaining safety floor, not the admission budget. */
static inline const char *map_lane_memory_reason(size_t internal,size_t dma,size_t psram,size_t largest,bool new_task,bool new_client){
    size_t reserve=(new_task?10*1024:0)+(new_client?14*1024:0);
    if(internal<32*1024+reserve)return "INTERNAL_RESERVE";
    if(dma<24*1024+reserve)return "DMA_RESERVE";
    if(new_task&&largest<12*1024)return "INTERNAL_FRAGMENTED";
    if(psram<(new_client?1536:512)*1024)return "PSRAM_RESERVE";
    return NULL;
}
