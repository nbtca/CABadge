#include "map_png.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *png_calloc(size_t n,size_t size){
    if(size&&n>65536u/size)return NULL;
    return heap_caps_calloc(n,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
#define calloc png_calloc
#endif
/* Confine upstream decoder allocations to PSRAM; other PNG users are unchanged. */
#include "pngle/pngle.c"
#ifdef ESP_PLATFORM
#undef calloc
#endif
struct map_png {pngle_t *decoder;uint16_t *pixels;bool done,invalid;};
static void init_image(pngle_t *p,uint32_t w,uint32_t h){
    map_png_t *m=pngle_get_user_data(p);m->invalid=w!=501||h!=1002;
}
static void draw_pixel(pngle_t *p,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint8_t rgba[4]){
    map_png_t *m=pngle_get_user_data(p);
    if(m->invalid||x>=500||y>=500)return;
    /* ceil(source*128/500), preserving the old floor(target*500/128) sampling.
     * Adam7 callbacks may cover several pixels; later passes overwrite them. */
    unsigned left=(x*128+499)/500,top=(y*128+499)/500;
    unsigned right=((x+w)*128+499)/500,bottom=((y+h)*128+499)/500;
    if(right>128)right=128;
    if(bottom>128)bottom=128;
    uint16_t color=rgba[3]?((rgba[0]>>3)<<11)|((rgba[1]>>2)<<5)|(rgba[2]>>3):0x1082;
    for(unsigned dy=top;dy<bottom;dy++)for(unsigned dx=left;dx<right;dx++)m->pixels[dy*128+dx]=color;
}
static void done_image(pngle_t *p){((map_png_t*)pngle_get_user_data(p))->done=true;}
map_png_t *map_png_create(uint16_t *pixels){
    if(!pixels)return NULL;
#ifdef ESP_PLATFORM
    map_png_t *m=png_calloc(1,sizeof(*m));
#else
    map_png_t *m=calloc(1,sizeof(*m));
#endif
    if(!m)return NULL;
    m->pixels=pixels;m->decoder=pngle_new();if(!m->decoder){free(m);return NULL;}
    for(unsigned i=0;i<128*128;i++)pixels[i]=0x1082;
    pngle_set_user_data(m->decoder,m);pngle_set_init_callback(m->decoder,init_image);
    pngle_set_draw_callback(m->decoder,draw_pixel);pngle_set_done_callback(m->decoder,done_image);return m;
}
int map_png_feed(map_png_t *m,const void *data,size_t size){
    int n=pngle_feed(m->decoder,data,size);return m->invalid?-1:n;
}
bool map_png_done(const map_png_t *m){return m->done&&!m->invalid;}
void map_png_destroy(map_png_t *m){if(m){pngle_destroy(m->decoder);free(m);}}
