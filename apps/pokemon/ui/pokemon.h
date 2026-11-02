#ifndef CABADGE_POKEMON_H
#define CABADGE_POKEMON_H
#include "badge_ui.h"
void pokemon_open(lv_obj_t *parent,void (*back)(void));
void pokemon_close(void);
void pokemon_poll(bool active);
void pokemon_info(char *out,size_t size);
#endif
