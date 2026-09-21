#pragma once
#include "lvgl.h"
void physical_display_init(void);
void physical_display_flush(const lv_area_t *area,const uint8_t *pixels);
void physical_display_backlight(int brightness,bool asleep);
void physical_touch_init(void);
void physical_touch_read(lv_indev_t *indev,lv_indev_data_t *data);
uint32_t physical_display_flush_us(void);
uint32_t physical_display_copy_us(void);
int physical_display_error(void);

typedef struct {uint32_t queue_us,wire_us,wait_us;} lcd_profile_t;
void physical_display_profile(bool on);
lcd_profile_t physical_display_profile_result(void);
int physical_display_clock_hz(void);
