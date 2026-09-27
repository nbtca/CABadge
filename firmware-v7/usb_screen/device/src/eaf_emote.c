/* Optional esp_emote_gfx 3.0.5 prototype; the normal build keeps eaf_wallpaper.c. */
#include "eaf_wallpaper.h"
#include "eaf_validate.h"
#include "physical_display.h"
#include "gfx.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gfx_handle_t core;
static gfx_disp_t *display;
static gfx_obj_t *animation;
static lv_obj_t *still;
static lv_image_dsc_t picture;
/* GUI always owns the emote recursive lock while paused. The render task must
 * not even WRITE the borrowed staging buffers while LVGL owns the LCD. */
static bool locked,playing,held,capturing;
static unsigned frames,creates;
static size_t source_bytes;
static int internal_before,psram_before;
static portMUX_TYPE stats_mux=portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint64_t interval,worst,render,render_max,submit,drain;
    int64_t last;
    uint32_t completed,render_frames,calls,errors;
    int internal_delta,psram_delta;
} stats;

static void flush(gfx_disp_t *disp,int x1,int y1,int x2,int y2,const void *data){
    bool last=gfx_disp_is_flushing_last(disp);
    if(capturing){
        uint16_t *out=(uint16_t*)picture.data;const uint16_t *in=data;
        for(int y=y1;y<y2;y++)for(int x=x1;x<x2;x++)out[y*360+x]=__builtin_bswap16(*in++);
    }else if(playing){
        /* render_time_us is accumulated before each flush; the last chunk
         * therefore contains this whole frame's decode/draw time. */
        gfx_disp_perf_stats_t prior={0};
        if(last)gfx_disp_get_perf_stats(disp,&prior);
        int64_t at=esp_timer_get_time();
        esp_err_t rc=physical_display_transition_draw(x1,y1,x2,y2,data);
        uint64_t submit=esp_timer_get_time()-at;at=esp_timer_get_time();
        esp_err_t drained=physical_display_transition_drain();
        uint64_t drain=esp_timer_get_time()-at;int64_t now=esp_timer_get_time();
        portENTER_CRITICAL(&stats_mux);
        if(last){stats.render+=prior.render_time_us;stats.render_frames++;if(prior.render_time_us>stats.render_max)stats.render_max=prior.render_time_us;}
        stats.calls++;stats.submit+=submit;stats.drain+=drain;
        if(rc!=ESP_OK||drained!=ESP_OK)stats.errors++;
        if(last){
            if(stats.last){uint64_t d=now-stats.last;stats.interval+=d;if(d>stats.worst)stats.worst=d;}
            stats.last=now;stats.completed++;
        }
        portEXIT_CRITICAL(&stats_mux);

    }
    /* Sync drain is deliberate for this prototype: the stock renderer waits
     * per block anyway. No ready notification while SPI still reads the block. */
    gfx_disp_flush_ready(disp,false);
}
static bool freeze_frame(void){
    if(!picture.data)picture.data=heap_caps_malloc(259200,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!picture.data)return false;
    capturing=true;gfx_disp_refresh_all(display);esp_err_t rc=gfx_refr_now(core);capturing=false;
    lv_image_cache_drop(&picture);
    if(still){lv_image_set_src(still,&picture);lv_obj_remove_flag(still,LV_OBJ_FLAG_HIDDEN);lv_obj_invalidate(still);}
    return rc==ESP_OK;
}
bool eaf_wallpaper_valid(const void *data,size_t size){return eaf_validate(data,size);}
void eaf_wallpaper_visible(bool visible){
    visible=visible&&!held;
    if(playing&&!visible){
        gfx_emote_lock(core);locked=true; /* Joins a whole render and all flushes. */
        gfx_anim_stop(animation);playing=false;
        if(!freeze_frame()&&still)lv_obj_add_flag(still,LV_OBJ_FLAG_HIDDEN);
        physical_display_transition_release();
    }
    if(!visible||!core||playing)return;
    if(!physical_display_transition_acquire())return;
    if(gfx_anim_start(animation)!=ESP_OK){physical_display_transition_release();return;}
    /* No full-frame allocation during playback. A frame is materialized only
     * on handoff for existing LVGL transitions/thumbnail readers. */
    if(still)lv_obj_add_flag(still,LV_OBJ_FLAG_HIDDEN);
    lv_image_cache_drop(&picture);free((void*)picture.data);picture.data=NULL;
    portENTER_CRITICAL(&stats_mux);memset(&stats,0,sizeof(stats));stats.internal_delta=internal_before;stats.psram_delta=psram_before;portEXIT_CRITICAL(&stats_mux);
    playing=true;locked=false;gfx_emote_unlock(core);
}
void eaf_wallpaper_hold(bool hold){held=hold;if(hold)eaf_wallpaper_visible(false);}
void eaf_wallpaper_clear(void){
    eaf_wallpaper_visible(false);
    if(still){lv_obj_delete(still);still=NULL;}
    lv_image_cache_drop(&picture);free((void*)picture.data);picture.data=NULL;
    if(core){
        if(!locked)gfx_emote_lock(core);
        if(animation){gfx_obj_delete(animation);animation=NULL;}
        if(display){gfx_disp_del(display);free(display);display=NULL;}
        gfx_emote_unlock(core);locked=false;
        /* No display/buffer remains when the worker is unblocked for deinit. */
        gfx_emote_deinit(core);core=NULL;
    }
}
bool eaf_wallpaper_set(lv_obj_t *parent,const void *data,size_t size){
    if(!eaf_wallpaper_valid(data,size))return false;
    eaf_wallpaper_clear();
    if(!physical_display_transition_acquire())return false;
    internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    psram_before=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    gfx_log_set_level_all(GFX_LOG_LEVEL_NONE); /* Preserve the binary USB protocol. */
    gfx_core_config_t config={.fps=30,.task=GFX_EMOTE_INIT_CONFIG()};
    core=gfx_emote_init(&config);if(!core)goto fail;
    gfx_emote_lock(core);locked=true;
    gfx_disp_config_t dc={.h_res=360,.v_res=360,.flush_cb=flush,.flags={.swap=1},
        .buffers={.buf1=physical_display_transition_buffer(0),.buf2=physical_display_transition_buffer(1),.buf_pixels=360*PHYSICAL_STAGING_ROWS}};
    display=gfx_disp_add(core,&dc);if(!display)goto fail;
    gfx_disp_set_bg_enable(display,false);
    animation=gfx_anim_create(display);if(!animation)goto fail;
    frames=jx_u32((const uint8_t*)data+4);source_bytes=size;
    if(gfx_anim_set_src(animation,data,size)!=ESP_OK||gfx_anim_set_segment(animation,0,frames-1,30,true)!=ESP_OK||gfx_anim_start(animation)!=ESP_OK)goto fail;
    gfx_anim_stop(animation);
    picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=259200};
    if(!freeze_frame())goto fail;
    still=lv_image_create(parent);if(!still)goto fail;
    lv_image_set_src(still,&picture);lv_obj_set_pos(still,0,0);lv_obj_remove_flag(still,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    int internal=internal_before-(int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    int psram=psram_before-(int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    /* Compare allocation deltas at creation, before service/thumbnail work.
     * The frozen frame is freed during playback; report that explicitly. */
    internal_before=internal;psram_before=psram-259200;
    creates++;physical_display_transition_release();return true;
fail:
    eaf_wallpaper_clear();physical_display_transition_release();return false;
}
const lv_image_dsc_t *eaf_wallpaper_frame(void){eaf_wallpaper_visible(false);return core&&picture.data?&picture:NULL;}
void eaf_wallpaper_info(char *out,size_t size){
    portENTER_CRITICAL(&stats_mux);__typeof__(stats) p=stats;portEXIT_CRITICAL(&stats_mux);
    unsigned n=p.completed?p.completed:1,r=p.render_frames?p.render_frames:1,g=p.completed>1?p.completed-1:1;
    snprintf(out,size,"{\"backend\":\"emote-3.0.5-prototype\",\"loaded\":%s,\"playing\":%s,\"direct\":%s,\"frames\":%u,\"delay_ms\":33.333,\"source_bytes\":%u,\"frame_bytes\":%u,\"creates\":%u,\"internal_delta\":%d,\"psram_delta\":%d,\"timing\":{\"completed\":%u,\"render_us\":%llu,\"render_max_us\":%llu,\"submit_us\":%llu,\"dma_wait_us\":%llu,\"interval_us\":%llu,\"worst_interval_us\":%llu,\"transactions\":%u,\"errors\":%u}}",
        core?"true":"false",playing?"true":"false",playing?"true":"false",frames,(unsigned)source_bytes,picture.data?259200u:0u,creates,p.internal_delta,p.psram_delta,(unsigned)p.completed,
        (unsigned long long)(p.render/r),(unsigned long long)p.render_max,(unsigned long long)(p.submit/n),(unsigned long long)(p.drain/n),(unsigned long long)(p.interval/g),(unsigned long long)p.worst,(unsigned)(p.calls/n),(unsigned)p.errors);
}
