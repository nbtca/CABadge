#include "map_cache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static bool same(map_key_t a,map_key_t b){return a.world==b.world&&a.lod==b.lod&&a.x==b.x&&a.z==b.z;}
map_cached_tile_t *map_cache_find(map_cache_t *c,map_key_t key,int64_t now){
    for(int i=0;i<MAP_CACHE_COUNT;i++)if(c->tile[i].pixels&&same(c->tile[i].key,key)){c->tile[i].used=now;return &c->tile[i];}
    return NULL;
}
map_cached_tile_t *map_cache_put(map_cache_t *c,map_key_t key,uint16_t *pixels,int64_t now,bool pin){
    map_cached_tile_t *slot=map_cache_find(c,key,now);
    if(!slot)for(int i=0;i<MAP_CACHE_COUNT;i++){
        map_cached_tile_t *t=&c->tile[i];
        if(t->pinned)continue;
        if(!t->pixels){slot=t;break;}
        if(!slot||t->used<slot->used)slot=t;
    }
    if(!slot)return NULL; /* Caller still owns pixels on failure. */
    free(slot->pixels);*slot=(map_cached_tile_t){.key=key,.pixels=pixels,.updated=now,.used=now,.pinned=pin};return slot;
}
void map_cache_unpin(map_cache_t *c){for(int i=0;i<MAP_CACHE_COUNT;i++)c->tile[i].pinned=false;}
bool map_cache_evict_cold(map_cache_t *c){
    map_cached_tile_t *old=NULL;
    for(int i=0;i<MAP_CACHE_COUNT;i++)if(c->tile[i].pixels&&!c->tile[i].pinned&&(!old||c->tile[i].used<old->used))old=&c->tile[i];
    if(!old)return false;
    free(old->pixels);*old=(map_cached_tile_t){0};return true;
}
void map_cache_clear(map_cache_t *c){for(int i=0;i<MAP_CACHE_COUNT;i++)free(c->tile[i].pixels);memset(c,0,sizeof(*c));}
static float span(int lod){return lod==1?500:lod==2?2500:12500;}
static void axis(int tile,float center,float bpp,float size,int *first,int *count){
    int lo=360,hi=-1;
    for(int i=0;i<360;i++)if((int)floorf((center+(i-180)*bpp)/size)==tile){if(lo==360)lo=i;hi=i;}
    *first=lo;*count=hi<lo?0:hi-lo+1;
}
void map_tile_bounds(map_key_t key,float cx,float cz,float bpp,int *x,int *y,int *w,int *h){
    axis(key.x,cx,bpp,span(key.lod),x,w);axis(key.z,cz,bpp,span(key.lod),y,h);
}
void map_tile_sample(const map_cached_tile_t *t,float cx,float cz,float bpp,int x,int y,int w,int h,uint16_t *out,int stride){
    float size=span(t->key.lod);
    for(int dy=0;dy<h;dy++){
        int pz=(int)((cz+(y+dy-180)*bpp-t->key.z*size)/size*MAP_TILE_SIZE);
        for(int dx=0;dx<w;dx++){
            int px=(int)((cx+(x+dx-180)*bpp-t->key.x*size)/size*MAP_TILE_SIZE);
            out[dy*stride+dx]=(px>=0&&px<MAP_TILE_SIZE&&pz>=0&&pz<MAP_TILE_SIZE)?t->pixels[pz*MAP_TILE_SIZE+px]:0x1082;
        }
    }
}
