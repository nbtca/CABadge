#ifndef CABADGE_TIMETILES_EFFECTS_H
#define CABADGE_TIMETILES_EFFECTS_H
#include "raster.h"

enum { TT_TOUCH_DOWN, TT_TOUCH_MOVE, TT_TOUCH_UP, TT_TOUCH_CANCEL };
enum { TT_EFFECT_CLOCK, TT_EFFECT_NOVA=8, TT_EFFECT_MOSAIC, TT_EFFECT_CODE, TT_EFFECT_FLY };
enum { TT_TAP_GAP=420,TT_TITLE_LEFT=120,TT_TITLE_RIGHT=240,TT_TITLE_TOP=28,TT_TITLE_BOTTOM=96 };
enum { TT_CLOCK_X=22,TT_CLOCK_Y=115,TT_CLOCK_COLUMN=11,TT_CLOCK_ROW=13,TT_CLOCK_TILE=9 };
typedef struct {
    int32_t x,y,vx,vy;
    int16_t home_x,home_y;
    uint8_t color;
} tt_tile_t;
typedef struct {
    tt_tile_t tiles[336];
    unsigned count,mode,taps;
    char text[6];
    int x,y,origin_x,origin_y,tap_x,tap_y,radius;
    bool held,moved,long_press,active,tap_title,theme_advance;
    uint64_t down_at,tap_at,started,release_at,last_step;
    uint32_t seed;
} tt_effect_t;

void tt_effect_clock(tt_effect_t *e,const char *text);
void tt_effect_cancel(tt_effect_t *e);
void tt_effect_pointer(tt_effect_t *e,unsigned event,int x,int y,uint64_t now);
bool tt_effect_shake(tt_effect_t *e,uint64_t now);
bool tt_effect_tick(tt_effect_t *e,uint64_t now);
void tt_effect_draw(const tt_effect_t *e,tt_canvas_t *canvas,uint64_t now);
#endif
