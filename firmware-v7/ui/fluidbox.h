#pragma once
#include "badge_ui.h"

bool fluidbox_open(lv_obj_t *parent);
void fluidbox_close(void);
void fluidbox_tick(bool awake);
void fluidbox_motion(const int16_t xyz[3], bool available);
void fluidbox_mode(int mode);
void fluidbox_next(void);
void fluidbox_reset(void);
void fluidbox_info(char *out, size_t size);
