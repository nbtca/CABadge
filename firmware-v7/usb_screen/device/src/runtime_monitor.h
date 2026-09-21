#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {MON_USB,MON_WIFI,MON_BLE,MON_MOTION,MON_WALL,MON_BATTERY,MON_MANAGEMENT,MON_GUI,MON_GROUPS};
void runtime_event(lv_event_t *event);
bool runtime_content_frame(void);
void runtime_flush(uint32_t us,uint32_t blocked,bool last,bool ok,bool content);
void runtime_poll(void);
int64_t runtime_begin(void);
void runtime_end(unsigned group,int64_t start);
bool runtime_snapshot(char *out,size_t capacity);
