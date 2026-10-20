#pragma once
#include "badge_ui.h"
void timetiles_init(void);
bool timetiles_open(lv_obj_t *parent,void (*back)(void));
void timetiles_close(void);
void timetiles_poll(bool visible);
void timetiles_motion(const int16_t xyz[3],bool available);
void timetiles_shake(void);
void timetiles_info(char *out,size_t size);
bool timetiles_command(const uint8_t *data,size_t size);
