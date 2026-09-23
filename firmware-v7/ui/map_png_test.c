/* Decode a real BlueMap PNG with the shipped LVGL decoder; no display/simulator.
 * Only private draw-buffer allocation and libc glue are substituted on the host. */
#include "src/libs/lodepng/lodepng.h"
#include "src/core/lv_global.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
lv_global_t lv_global;
static size_t rejected;
void *lodepng_malloc(size_t n){if(n>2100000){rejected=n;return NULL;}return malloc(n);}
void *lodepng_realloc(void *p,size_t n){if(n>2100000){rejected=n;return NULL;}return realloc(p,n);}
void lodepng_free(void *p){free(p);}
void *lv_memcpy(void *d,const void *s,size_t n){return memcpy(d,s,n);}
void lv_memset(void *d,uint8_t v,size_t n){memset(d,v,n);}
lv_draw_buf_t *lv_draw_buf_create_ex(const lv_draw_buf_handlers_t *h,uint32_t w,uint32_t ht,lv_color_format_t cf,uint32_t stride){
    (void)h;(void)cf;lv_draw_buf_t *b=calloc(1,sizeof(*b));assert(b);
    b->data_size=ht*stride;b->data=malloc(b->data_size);assert(b->data);b->header.w=w;b->header.h=ht;b->header.stride=stride;return b;
}
void lv_draw_buf_destroy(lv_draw_buf_t *b){if(b){free(b->data);free(b);}}
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    unsigned char *png=malloc(n);assert(png&&fread(png,1,n,f)==(size_t)n);fclose(f);
    lv_draw_buf_t *b=NULL;unsigned w=0,h=0,error=lodepng_decode32((unsigned char**)&b,&w,&h,png,n);
    printf("PNG error=%u width=%u height=%u rejected_allocation=%zu\n",error,w,h,rejected);fflush(stdout);
    assert(!error&&b&&w==501&&h==1002);lv_draw_buf_destroy(b);free(png);return 0;
}
