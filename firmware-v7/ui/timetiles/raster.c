#include "raster.h"
#include "timetiles_assets.h"
#include <limits.h>
#include <string.h>

enum { SIDE = 360, BASE = 0x0041 };

static uint16_t blend565(uint16_t bg, uint16_t fg, unsigned alpha)
{
    unsigned inverse = 15 - alpha;
    return (uint16_t)((((fg >> 11) * alpha + 7 + (bg >> 11) * inverse) / 15 << 11)
        | ((((fg >> 5 & 63) * alpha + 7 + (bg >> 5 & 63) * inverse) / 15) << 5)
        | (((fg & 31) * alpha + 7 + (bg & 31) * inverse) / 15));
}

static void pixel(tt_canvas_t *c, int x, int y, uint16_t color, unsigned alpha)
{
    if ((unsigned)x >= SIDE || (unsigned)y >= SIDE || !alpha) return;
    int px = x, py = y;
    switch (c->rotation) {
    case 1: px = y; py = SIDE - 1 - x; break;
    case 2: px = SIDE - 1 - x; py = SIDE - 1 - y; break;
    case 3: px = SIDE - 1 - y; py = x; break;
    }
    uint16_t *dest = c->pixels + py * SIDE + px;
    *dest = alpha == 15 ? color : blend565(*dest, color, alpha);
}

static void rectangle(tt_canvas_t *c, int x, int y, int w, int h, uint16_t color, unsigned alpha)
{
    if (w <= 0 || h <= 0) return;
    int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    if (right > SIDE) right = SIDE;
    if (bottom > SIDE) bottom = SIDE;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int row = y; row < bottom; row++)
        for (int column = x; column < right; column++) pixel(c, column, row, color, alpha);
}

void tt_rect(tt_canvas_t *c, int x, int y, int w, int h, uint16_t color)
{
    rectangle(c, x, y, w, h, color, 15);
}

void tt_raster_begin(tt_canvas_t *c, uint16_t *pixels, unsigned theme,
                     unsigned rotation, bool grid, bool glow)
{
    c->pixels = pixels;
    c->rotation = rotation & 3;
    c->grid = grid;
    c->glow = glow;
    memcpy(c->color, tt_theme_rgb565[theme % 6], sizeof(c->color));
    for (unsigned i = 0; i < SIDE * SIDE; i++) pixels[i] = BASE;
    if (grid) {
        for (int p = 0; p < SIDE; p += 18) {
            unsigned alpha = p % 54 == 0 ? 12 : 6;
            rectangle(c, p, 0, 1, SIDE, c->color[6], alpha);
            rectangle(c, 0, p, SIDE, 1, c->color[6], alpha);
        }
    }
}

static int scaled(unsigned length, unsigned percent)
{
    uint64_t value = ((uint64_t)length * percent + 50) / 100;
    return value > INT_MAX ? INT_MAX : (int)value;
}

int tt_mask_width(unsigned id, unsigned percent)
{
    return id < 119 ? scaled(tt_masks[id].width, percent) : 0;
}

void tt_mask(tt_canvas_t *c, unsigned id, int x, int y, unsigned percent, uint16_t color)
{
    if (id >= 119 || !percent) return;
    const tt_mask_meta_t *mask = &tt_masks[id];
    int width = scaled(mask->width, percent), height = scaled(mask->height, percent);
    if (!width || !height) return;
    int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
    if (right > SIDE) right = SIDE;
    if (bottom > SIDE) bottom = SIDE;
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    unsigned row_cursor = mask->offset, row_end = tt_mask_rle[row_cursor];
    for (int row = top; row < bottom; row++) {
        unsigned source_y = (unsigned)(((uint64_t)((int64_t)row - y) * mask->height) / height);
        while (source_y * mask->width >= row_end) {
            row_cursor += 2;
            row_end += tt_mask_rle[row_cursor];
        }
        unsigned cursor = row_cursor, end = row_end;
        for (int column = left; column < right; column++) {
            unsigned source_x = (unsigned)(((uint64_t)((int64_t)column - x) * mask->width) / width);
            unsigned source = source_y * mask->width + source_x;
            while (source >= end) {
                cursor += 2;
                end += tt_mask_rle[cursor];
            }
            pixel(c, column, row, color, tt_mask_rle[cursor + 1]);
        }
    }
}

void tt_small(tt_canvas_t *c, const char *text, int x, int y, unsigned scale, uint16_t color)
{
    if (!scale || scale > SIDE) return;
    while (*text) {
        unsigned ch = (unsigned char)*text++, id = 39;
        if (ch >= '0' && ch <= '9') id = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') id = ch - 'A' + 10;
        else if (ch == ':') id = 36;
        else if (ch == '.') id = 37;
        else if (ch == '-') id = 38;
        if (id < 39) {
            for (unsigned row = 0; row < 7; row++)
                for (unsigned column = 0; column < 5; column++)
                    if (tt_small_font[id][row] & (1u << (4 - column)))
                        tt_rect(c, x + (int)(column * scale), y + (int)(row * scale), scale, scale, color);
        }
        x += 6 * (int)scale;
    }
}

void tt_tile(tt_canvas_t *c, int x, int y, int size, uint16_t color)
{
    if (c->glow) {
        // shortcut: rectangular soft layers follow the uncached original; compare cached glow on the board before refining.
        rectangle(c, x - 3, y - 3, size + 6, size + 6, color, 1);
        rectangle(c, x - 1, y - 1, size + 2, size + 2, color, 3);
    }
    tt_rect(c, x, y, size, size, color);
}

void tt_digits(tt_canvas_t *c, const char *text, int x, int y, unsigned scale, uint16_t color)
{
    if (!scale || scale > SIDE) return;
    int size = scale > 2 ? (int)scale - 2 : 1;
    while (*text) {
        unsigned ch = (unsigned char)*text++;
        if (ch == ':') {
            tt_tile(c, x, y + 3 * (int)scale, size, color);
            tt_tile(c, x, y + 7 * (int)scale, size, color);
            x += 2 * (int)scale;
        } else {
            unsigned id = ch >= '0' && ch <= '9' ? ch - '0' : 10;
            for (unsigned row = 0; row < 10; row++)
                for (unsigned column = 0; column < 6; column++)
                    if (tt_digit_font[id][row] & (1u << (5 - column)))
                        tt_tile(c, x + (int)(column * scale), y + (int)(row * scale), size, color);
            x += 7 * (int)scale;
        }
    }
}

void tt_icon(tt_canvas_t *c, unsigned icon, int x, int y, unsigned scale, uint16_t color)
{
    if (icon >= 14 || !scale || scale > SIDE) return;
    for (unsigned row = 0; row < 16; row++)
        for (unsigned column = 0; column < 16; column++)
            if (tt_icons[icon][row] & (1u << (15 - column)))
                tt_rect(c, x + (int)(column * scale), y + (int)(row * scale), scale, scale, color);
}
