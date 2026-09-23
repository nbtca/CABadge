#ifndef BADGE_APPS_H
#define BADGE_APPS_H
#include "badge_ui.h"
#include "miner.h"
#define MAP_SIZE 360
typedef struct {float x,z;char name[33];} map_player_t;
typedef struct {uint32_t serial;int world,zoom;float x,z;} map_view_t;
typedef struct {map_view_t view;uint16_t *pixels;int players,count,tiles,error;map_player_t player[12];} map_result_t;
extern const lv_image_dsc_t *const miner_sprites[53];
void badge_apps_create(lv_obj_t *parent);
void badge_apps_open(void);
void badge_apps_hide(void);
void badge_apps_bind(bool (*map)(const map_view_t*),void (*save)(uint64_t,int));
void badge_apps_saved(uint64_t dex,int best);
void badge_apps_launch(int app); /* 0 list, 1 map, 2 playable miner, 3 Grok */
void badge_apps_status(int *page,int *loading,int *error,int *tiles,int *players);
void badge_apps_map_result(map_result_t *result); /* consumes pixels on GUI task */
void app_service_init(void);
void app_service_poll(void);
#endif
