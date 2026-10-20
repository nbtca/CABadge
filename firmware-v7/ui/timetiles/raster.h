#ifndef CABADGE_TIMETILES_RASTER_H
#define CABADGE_TIMETILES_RASTER_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t *pixels;
    unsigned rotation;
    bool grid, glow;
    uint16_t color[7];
} tt_canvas_t;

void tt_raster_begin(tt_canvas_t *c, uint16_t *pixels, unsigned theme,
                     unsigned rotation, bool grid, bool glow);
void tt_rect(tt_canvas_t *c, int x, int y, int w, int h, uint16_t color);
void tt_mask(tt_canvas_t *c, unsigned id, int x, int y, unsigned percent, uint16_t color);
int tt_mask_width(unsigned id, unsigned percent);
void tt_small(tt_canvas_t *c, const char *text, int x, int y, unsigned scale, uint16_t color);
void tt_digits(tt_canvas_t *c, const char *text, int x, int y, unsigned scale, uint16_t color);
void tt_tile(tt_canvas_t *c, int x, int y, int size, uint16_t color);
void tt_icon(tt_canvas_t *c, unsigned icon, int x, int y, unsigned scale, uint16_t color);
#endif
