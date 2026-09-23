#pragma once
#include "display_perf.h"
#if CABADGE_DISPLAY_PERF
void frame_trace(unsigned code, uint32_t value);
void frame_trace_begin(uint32_t id);
void frame_trace_end(void);
size_t frame_trace_take(void *out, size_t capacity);
#else
static inline void frame_trace(unsigned code,uint32_t value){(void)code;(void)value;}
static inline void frame_trace_begin(uint32_t id){(void)id;}
static inline void frame_trace_end(void){}
static inline size_t frame_trace_take(void *out,size_t capacity){(void)out;(void)capacity;return 0;}
#endif
