#pragma once
#include "lvgl.h"
#include <stddef.h>
#include <stdbool.h>
bool eaf_wallpaper_valid(const void *data,size_t size);
bool eaf_wallpaper_set(lv_obj_t *parent,const void *data,size_t size);
void eaf_wallpaper_clear(void);
void eaf_wallpaper_visible(bool visible);
void eaf_wallpaper_hold(bool hold);
const lv_image_dsc_t *eaf_wallpaper_frame(void);
void eaf_wallpaper_info(char *out,size_t size);
