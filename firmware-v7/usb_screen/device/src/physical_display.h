#pragma once
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
void physical_display_init(void);
esp_lcd_panel_handle_t physical_display_panel(void);
esp_lcd_panel_io_handle_t physical_display_io(void);
void physical_display_bind(lv_display_t *display);
esp_err_t physical_display_draw(int x1,int y1,int x2,int y2,const void *pixels);
void physical_display_wait(void);
bool physical_display_complete(uint32_t *us);
void physical_display_backlight(int brightness,bool asleep);
void physical_touch_init(void);
void physical_touch_read(lv_indev_t *indev,lv_indev_data_t *data);
int physical_display_error(void);
int physical_display_clock_hz(void);
const char *physical_display_readback(void);
unsigned physical_display_backlight_duty(void);

/* GUI task owns handoff; the compositor worker owns submissions until release. */
bool physical_display_transition_acquire(void);
esp_err_t physical_display_transition_draw(int x1,int y1,int x2,int y2,const void *pixels);
esp_err_t physical_display_transition_drain(void);
void physical_display_transition_release(void);
bool physical_display_transition_active(void);
/* One shared pool. LVGL and compositor ownership are mutually exclusive. */
#define PHYSICAL_STAGING_ROWS 16
void *physical_display_transition_buffer(unsigned index);
