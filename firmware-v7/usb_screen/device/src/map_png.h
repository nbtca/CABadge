#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct map_png map_png_t;
/* Caller owns the 128x128 RGB565 destination until destroy. No full image. */
map_png_t *map_png_create(uint16_t *pixels);
int map_png_feed(map_png_t *png,const void *data,size_t size);
bool map_png_done(const map_png_t *png);
void map_png_destroy(map_png_t *png);
