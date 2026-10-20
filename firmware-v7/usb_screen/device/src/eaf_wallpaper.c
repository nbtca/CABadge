#include "eaf_wallpaper.h"
#include "eaf_validate.h"
#include "wallpaper_service.h"
#include "wallpaper_timing.h"
#include "esp_eaf_dec.h"
#include "ui/ui_transition_compositor.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
_Static_assert(LV_DRAW_SW_SUPPORT_RGB565_SWAPPED,"EAF frozen frame needs the existing swapped RGB565 renderer");
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    esp_eaf_format_handle_t decoder;
    lv_image_dsc_t picture;
    uint8_t *pixels; /* Owner: picture.data may borrow a source frame during the experiment. */
    uint8_t indices[360*24]; /* Validated RLE profile: one reusable block. */
    bool native,first;
    const uint8_t *timing; /* Immutable Flash mapping; stop joins before unmap. */
    atomic_uint frame;
    unsigned frames;
    size_t source_bytes;
#if CABADGE_EAF_INTERPOLATE
    uint8_t *source[2];
    unsigned slot;
    bool interpolate,midpoint_next;
#endif
} eaf_backend_t;
static eaf_backend_t *backend;
/* This ordinary image is frozen while Direct owns the LCD. It is only rendered
 * by LVGL after handoff, for overlays/gestures/transitions, never per EAF frame. */
static lv_obj_t *still;
static bool held,playing,fault;
static unsigned creates;
static int internal_delta,psram_delta;
#if CABADGE_EAF_INTERPOLATE
static bool interpolate_requested;
#endif
bool eaf_wallpaper_valid(const void *data,size_t size){return eaf_validate(data,size);}
static bool decode_into(eaf_backend_t *b,unsigned index,bool native,uint8_t *destination){
    const uint8_t *data=esp_eaf_format_get_frame_data(b->decoder,index);
    esp_eaf_header_t header={0};
    if(!data||esp_eaf_header_parse(data,esp_eaf_format_get_frame_size(b->decoder,index),&header)!=ESP_EAF_FORMAT_VALID){esp_eaf_free_header(&header);return false;}
    /* Keep the official RLE decoder and palette conversion. Avoid its generic
     * per-block allocation and per-pixel alpha/cache dispatch for our validated
     * opaque RLE8 profile. The small palette fits in the existing worker stack. */
    uint16_t palette[256];
    for(unsigned i=0;i<256;i++)palette[i]=esp_eaf_palette_get_color(&header,i,native).full;
    bool ok=true;size_t offset=header.data_offset;
    for(unsigned block=0;block<header.blocks;block++){
        unsigned row=block*header.block_height,rows=360-row;
        if(rows>header.block_height)rows=header.block_height;
        size_t count=rows*360u,decoded=count;
        if(esp_eaf_rle_decode(data+offset+1,header.block_len[block]-1,b->indices,&decoded,false)!=ESP_OK||decoded!=count){ok=false;break;}
        uint16_t *pixels=(uint16_t*)destination+row*360u;
        for(size_t i=0;i<count;i++)pixels[i]=palette[b->indices[i]];
        offset+=header.block_len[block];
    }
    esp_eaf_free_header(&header);
    return ok;
}
static bool decode(eaf_backend_t *b,unsigned index,bool native){
    bool ok=decode_into(b,index,native,(uint8_t*)b->picture.data);
    if(ok){b->native=native;atomic_store(&b->frame,index);}
    return ok;
}
#if CABADGE_EAF_INTERPOLATE
static void midpoint(uint16_t *out,const uint16_t *a,const uint16_t *b,size_t count){
    /* ponytail: linear frame blend can ghost moving edges; motion compensation is a separate experiment. */
    for(size_t i=0;i<count;i++){
        uint16_t x=__builtin_bswap16(a[i]),y=__builtin_bswap16(b[i]);
        out[i]=__builtin_bswap16((x&y)+(((x^y)&0xf7deu)>>1)); /* Per-channel floor mean, no RGB carry. */
    }
}
static bool configure_interpolation(eaf_backend_t *b,bool enabled){
    if(enabled){
        for(unsigned i=0;i<2;i++)if(!b->source[i])b->source[i]=heap_caps_malloc(259200,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!b->source[0]||!b->source[1])return false;
        if(!decode_into(b,atomic_load(&b->frame),true,b->source[0]))return false;
        b->slot=0;b->picture.data=b->source[0];
    }else{
        if(!decode_into(b,atomic_load(&b->frame),true,b->pixels))return false;
        b->picture.data=b->pixels;
    }
    b->interpolate=enabled;b->midpoint_next=false;return true;
}
static bool interpolated_frame(eaf_backend_t *b,ui_compositor_frame_t *frame,uint32_t *duration_us){
    unsigned index=atomic_load(&b->frame);
    uint32_t period=wall_timing_period(b->timing,index);
    if(b->first){b->first=false;b->midpoint_next=true;b->picture.data=b->source[b->slot];*duration_us=period/2;}
    else if(b->midpoint_next){
        int64_t at=esp_timer_get_time();
        if(!decode_into(b,(index+1)%b->frames,true,b->source[b->slot^1]))return false;
        frame->source_decode_us=esp_timer_get_time()-at;at=esp_timer_get_time();
        midpoint((uint16_t*)b->pixels,(const uint16_t*)b->source[b->slot],(const uint16_t*)b->source[b->slot^1],360u*360u);
        frame->blend_us=esp_timer_get_time()-at;frame->interpolated=true;
        b->picture.data=b->pixels;b->midpoint_next=false;*duration_us=period-period/2;
    }else{
        index=(index+1)%b->frames;atomic_store(&b->frame,index);b->slot^=1;
        b->picture.data=b->source[b->slot];b->midpoint_next=true;*duration_us=wall_timing_period(b->timing,index)/2;
    }
    frame->layers[0].pixels=b->picture.data;return true;
}
bool eaf_wallpaper_interpolate(bool enabled){
    if(!backend||(!playing&&ui_transition_compositor_active()))return false;
    bool resume=playing;eaf_wallpaper_visible(false); /* Join old DMA before changing source ownership. */
    bool ok=configure_interpolation(backend,enabled);
    if(ok)interpolate_requested=enabled;
    if(still){lv_image_cache_drop(&backend->picture);lv_obj_invalidate(still);}
    if(resume)eaf_wallpaper_visible(true);
    return ok;
}
#endif
static bool next_frame(void *context,ui_compositor_frame_t *frame,uint32_t *duration_us){
    eaf_backend_t *b=context;
    frame->source_decode_us=frame->blend_us=0;frame->interpolated=false;
#if CABADGE_EAF_INTERPOLATE
    if(b->interpolate)return interpolated_frame(b,frame,duration_us);
#endif
    unsigned index=(atomic_load(&b->frame)+(b->first?0:1))%b->frames;
    b->first=false;*duration_us=wall_timing_period(b->timing,index);
    int64_t at=esp_timer_get_time();bool ok=decode(b,index,true);frame->source_decode_us=esp_timer_get_time()-at;
    frame->layers[0].pixels=b->picture.data;return ok;
}
static void dispose(eaf_backend_t *b){
    if(!b)return;
    if(b->decoder)esp_eaf_format_deinit(b->decoder);
    free(b->pixels);
#if CABADGE_EAF_INTERPOLATE
    free(b->source[0]);free(b->source[1]);
#endif
    free(b);
}
void eaf_wallpaper_visible(bool visible){
    visible=visible&&!held;
    if(playing&&(!visible||ui_transition_compositor_stream_failed())){
        fault=visible;ui_transition_compositor_stop();playing=false;
        /* LVGL already supports RGB565_SWAPPED. Keep this single source in
         * LCD-native order, including idle/fallback rendering; never swap it in
         * place on a navigation handoff. Worker has joined before LVGL reads. */
        if(still){lv_image_cache_drop(&backend->picture);lv_obj_invalidate(still);}
    }
    if(!visible){fault=false;return;}
    if(!backend||playing||fault||ui_transition_compositor_active())return;
    ui_compositor_frame_t frame={.width=360,.height=360,.count=1,
        .layers={{.pixels=backend->picture.data,.stride=720,.width=360,.height=360}}};
    backend->first=true;
    playing=ui_transition_compositor_stream_begin(&frame,next_frame,backend,wall_timing_period(backend->timing,atomic_load(&backend->frame)));
}
void eaf_wallpaper_hold(bool hold){held=hold;if(held)eaf_wallpaper_visible(false);}
void eaf_wallpaper_clear(void){
    eaf_wallpaper_visible(false);
    if(still){lv_obj_delete(still);still=NULL;}
    if(backend)lv_image_cache_drop(&backend->picture);
    dispose(backend);backend=NULL;
}
bool eaf_wallpaper_set(lv_obj_t *parent,const void *data,size_t size){
    if(!eaf_wallpaper_valid(data,size))return false;
    int internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    int psram_before=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    eaf_backend_t *next=heap_caps_calloc(1,sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next)return false;
    next->picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565_SWAPPED,.w=360,.h=360,.stride=720},
        .data_size=259200,.data=heap_caps_malloc(259200,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)};
    next->pixels=(uint8_t*)next->picture.data;
    if(!next->picture.data||esp_eaf_format_init(data,size,&next->decoder)!=ESP_OK){dispose(next);return false;}
    next->frames=esp_eaf_format_get_total_frames(next->decoder);next->source_bytes=size;next->timing=wallpaper_eaf_timing(data);
    if(!decode(next,0,true)){dispose(next);return false;}
#if CABADGE_EAF_INTERPOLATE
    if(interpolate_requested&&!configure_interpolation(next,true)){dispose(next);return false;}
#endif
    lv_obj_t *image=lv_image_create(parent);if(!image){dispose(next);return false;}
    lv_image_set_src(image,&next->picture);lv_obj_set_pos(image,0,0);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    internal_delta=internal_before-(int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    psram_delta=psram_before-(int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    eaf_wallpaper_clear();backend=next;still=image;creates++;fault=false;
    return true;
}
const lv_image_dsc_t *eaf_wallpaper_frame(void){
    /* GUI-only borrowed frame for the existing thumbnail builder. Join before
     * reading; the next home visibility poll may resume the worker afterwards. */
    eaf_wallpaper_visible(false);return backend?&backend->picture:NULL;
}
void eaf_wallpaper_info(char *out,size_t size){
    char timing[1800];ui_transition_compositor_stream_stats(timing,sizeof(timing));
    bool available=false,enabled=false;unsigned extra=0;
#if CABADGE_EAF_INTERPOLATE
    available=true;enabled=backend&&backend->interpolate;
    if(backend)extra=(!!backend->source[0]+!!backend->source[1])*259200u;
#endif
    snprintf(out,size,"{\"loaded\":%s,\"playing\":%s,\"direct\":%s,\"frame\":%u,\"frames\":%u,\"delay_ms\":%.3f,\"timing_mode\":\"%s\",\"timing_crc\":%u,\"source_bytes\":%u,\"frame_bytes\":%u,\"creates\":%u,\"internal_delta\":%d,\"psram_delta\":%d,\"interpolation\":{\"available\":%s,\"enabled\":%s,\"method\":\"blend50\",\"extra_psram_bytes\":%u},\"timing\":%s}",
        backend?"true":"false",playing?"true":"false",playing&&ui_transition_compositor_stream_active()?"true":"false",
        backend?atomic_load(&backend->frame):0,backend?backend->frames:0,wall_timing_period(backend?backend->timing:NULL,backend?atomic_load(&backend->frame):0)/1000.0,
        !backend||!backend->timing?"LEGACY":jx_u16(backend->timing+6)==1?"CONSTANT":"PER_FRAME",(unsigned)(backend&&backend->timing?jx_u32(backend->timing+24):0),(unsigned)(backend?backend->source_bytes:0),backend?259200u:0u,
        creates,internal_delta,psram_delta,available?"true":"false",enabled?"true":"false",extra,timing);
}
