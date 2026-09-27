#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MAP_CACHE_COUNT 20
#define MAP_TILE_SIZE 128
typedef struct {int world,lod,x,z;} map_key_t;
typedef struct {map_key_t key;uint16_t *pixels;int64_t updated,used;bool pinned;} map_cached_tile_t;
typedef struct {map_cached_tile_t tile[MAP_CACHE_COUNT];} map_cache_t;
/* Only the coordinator accesses the cache. Downloaders transfer completed pixels. */
map_cached_tile_t *map_cache_find(map_cache_t *cache,map_key_t key,int64_t now);
map_cached_tile_t *map_cache_put(map_cache_t *cache,map_key_t key,uint16_t *pixels,int64_t now,bool pin);
void map_cache_clear(map_cache_t *cache);
void map_cache_unpin(map_cache_t *cache);
bool map_cache_evict_cold(map_cache_t *cache);
/* Screen bounds of a tile, using the same world-to-screen sampling as compose. */
void map_tile_bounds(map_key_t key,float cx,float cz,float bpp,int *x,int *y,int *w,int *h);
void map_tile_sample(const map_cached_tile_t *tile,float cx,float cz,float bpp,int x,int y,int w,int h,uint16_t *out,int stride);
