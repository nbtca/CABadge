#ifndef CABADGE_GROK_H
#define CABADGE_GROK_H
#include "badge_ui.h"
bool grok_open(lv_obj_t *parent,void (*back)(void));
void grok_close(void);
void grok_tick(uint32_t now);
void grok_motion(float x,float y,bool shake,bool available);
#endif
