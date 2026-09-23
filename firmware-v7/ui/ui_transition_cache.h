#pragma once
#include "lvgl.h"
#include <stddef.h>
/* All calls belong to the GUI task. A surface is a real opaque subtree. */
void ui_transition_cache_register(lv_obj_t *surface,bool forward_drag);
void ui_transition_cache_request(lv_obj_t *surface);
void ui_transition_cache_invalidate(lv_obj_t *object);
bool ui_transition_cache_begin(lv_obj_t *surface);
void ui_transition_cache_end(lv_obj_t *surface);
void ui_transition_cache_forget(lv_obj_t *surface);
void ui_transition_cache_trim(void);
void ui_transition_cache_show(lv_obj_t *surface,bool visible);
bool ui_transition_cache_visible(lv_obj_t *surface);
void ui_transition_cache_position(lv_obj_t *surface);
void ui_transition_cache_poll(bool idle);
bool ui_transition_cache_active(void);
void ui_transition_cache_stats(char *out,size_t size);
#if CABADGE_DISPLAY_PERF
void ui_transition_cache_test_mode(unsigned mode);
#endif

/* GUI-only scene submission. xy uses absolute LCD coordinates; no source LVGL
 * object is moved by this API. Returns false for the existing safe fallback. */
bool ui_transition_cache_direct(lv_obj_t *const *objects,const int *xy,unsigned count,int x,int y,int width,int height,uint16_t background);
void ui_transition_cache_direct_end(void);
bool ui_transition_cache_direct_active(void);

bool ui_transition_cache_ready(lv_obj_t *surface);
void ui_transition_cache_suspend(lv_obj_t *surface,bool suspended,bool retain);

void ui_transition_cache_identify(lv_obj_t *surface,unsigned page_id);
void ui_transition_cache_invalidate_reason(lv_obj_t *object,const char *reason);
bool ui_transition_cache_debug_take(char *out,size_t size);
void ui_transition_cache_debug_dump(void);

void ui_transition_cache_plan(const unsigned *page_ids,unsigned count);
void ui_transition_cache_keep(lv_obj_t *surface,bool hot);
void ui_transition_cache_alias(lv_obj_t *surface,lv_obj_t *other_view_of_same_content);
void ui_transition_cache_pause(bool paused);
void ui_transition_cache_reserve(size_t free_psram_bytes);
typedef enum { UI_MEMORY_NORMAL,UI_MEMORY_HIGH,UI_MEMORY_CRITICAL } ui_memory_pressure_t;
/* GUI-owned policy; worker completion returns to NORMAL through service_poll. */
void ui_memory_pressure_set(ui_memory_pressure_t pressure);
ui_memory_pressure_t ui_memory_pressure_get(void);
