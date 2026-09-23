#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef CABADGE_DISPLAY_PERF
#define CABADGE_DISPLAY_PERF 0
#endif
#if CABADGE_DISPLAY_PERF
void display_perf_transition_call_done(void);
void display_perf_transition_begin(int from,int to,bool reduced);
bool display_perf_transition_report(char *out,size_t capacity,bool pending);
void display_perf_event(lv_event_t *event);
void display_perf_complete(uint32_t us,bool last,bool ok);
bool display_perf_report(char *out,size_t capacity);
#else
static inline void display_perf_transition_call_done(void){}
static inline void display_perf_transition_begin(int from,int to,bool reduced){(void)from;(void)to;(void)reduced;}
static inline bool display_perf_transition_report(char *out,size_t capacity,bool pending){(void)out;(void)capacity;(void)pending;return false;}
static inline void display_perf_event(lv_event_t *event){(void)event;}
static inline void display_perf_complete(uint32_t us,bool last,bool ok){(void)us;(void)last;(void)ok;}
static inline bool display_perf_report(char *out,size_t capacity){(void)out;(void)capacity;return false;}
#endif
