#ifndef BADGE_APPS_H
#define BADGE_APPS_H
#include "badge_ui.h"
#define MAP_SIZE 360
typedef struct {float x,z;char name[33];} map_player_t;
typedef struct {uint32_t serial;int world,zoom;float x,z;} map_view_t;
typedef struct {map_view_t view;uint16_t *pixels;int players,count,tiles,error;bool partial,reset;int x,y,w,h;map_player_t player[12];} map_result_t;
void badge_apps_create(lv_obj_t *parent);
void badge_apps_open(void);
void badge_apps_hide(void);
void badge_apps_bind(bool (*map)(const map_view_t*));
void badge_apps_launch(int app); /* 0 list, 1 map, 3 Grok, 6 TimeTiles, 7 Pokemon; retired 2/4/5 show list. */
bool badge_apps_clock_sync(void); /* Shared nonblocking SNTP start; GUI or existing map worker. */
void badge_apps_motion(float x,float y,bool shake,bool available);
void badge_apps_status(int *page,int *loading,int *error,int *tiles,int *players);
bool badge_apps_map_accepting(void);
bool badge_apps_map_result(map_result_t *result); /* consumes pixels on GUI task */
void app_service_init(void);
void app_service_poll(void);
#endif
