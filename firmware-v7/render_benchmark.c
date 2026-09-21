/* Same physical UI and full-frame LVGL rasterizer; host timings are NOT ESP32 FPS. */
#define LV_KCONFIG_IGNORE 1
#define ESP_PLATFORM 1
#include "ui/badge_ui.h"
static bool reject_snapshot;
static lv_draw_buf_t *test_snapshot(lv_obj_t *obj,lv_color_format_t cf);
#define lv_snapshot_take test_snapshot
#include "ui/badge_ui.c"
#undef lv_snapshot_take
static lv_draw_buf_t *test_snapshot(lv_obj_t *obj,lv_color_format_t cf){return reject_snapshot?NULL:lv_snapshot_take(obj,cf);}
#include <windows.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint16_t frame[360*360],draw[360*360];
static bool partial;
static unsigned completed;
static bool watch_meter;
static void check_meter_area(lv_event_t *e){if(watch_meter)CHECK(badge_ui_perf_covers(lv_event_get_param(e)));}
static void no_auto_refresh(lv_timer_t *timer){lv_timer_pause(timer);}
static double us(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return t.QuadPart*1000000.0/f.QuadPart;}
static void flush_frame(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    int w=a->x2-a->x1+1,h=a->y2-a->y1+1;
    CHECK(a->x1>=0&&a->y1>=0&&a->x2<360&&a->y2<360);
    for(int y=0;y<h;y++)memcpy(frame+(a->y1+y)*360+a->x1,p+y*w*2,w*2);
    if(lv_display_flush_is_last(d))completed++;lv_display_flush_ready(d);
}
static void capture(const char *folder,int scene,int sample){
    char path[1024];snprintf(path,sizeof(path),"%s/scene-%d-%d.ppm",folder,scene,sample);
    FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n360 360\n255\n");
    for(int i=0;i<360*360;i++){uint16_t c=frame[i];unsigned char rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
int main(int argc,char **argv){
    partial=argc>2&&!strcmp(argv[2],"--partial");
    lv_init();lv_display_t *d=lv_display_create(360,360);CHECK(d);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,draw,NULL,sizeof(draw),partial?LV_DISPLAY_RENDER_MODE_PARTIAL:LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d,flush_frame);lv_timer_pause(lv_display_get_refr_timer(d));
    lv_timer_set_cb(lv_display_get_refr_timer(d),no_auto_refresh);
    badge_state_t state={.brightness=70,.wallpaper_index=0,.battery_mv=4030};
    lv_obj_remove_style_all(lv_screen_active());badge_ui_create(lv_screen_active(),&state,NULL);
    for(int scene=0;scene<3;scene++){
        badge_ui_probe_restore(0);double total=0,max=0;unsigned start=completed;
        for(unsigned t=0;t<4500;t+=33){
            badge_ui_probe_scene(scene,t);lv_tick_inc(33);lv_timer_handler();if(!partial)lv_obj_invalidate(lv_screen_active());
            double begin=us();lv_refr_now(d);double elapsed=us()-begin;
            if(t>=500){total+=elapsed;if(elapsed>max)max=elapsed;}
            if(argc>1&&(t==594||t==726||t==924))capture(argv[1],scene,t);
        }
        printf("scene=%d frames=%u host_render_mean_us=%.1f max_us=%.1f\n",scene,completed-start,total/121,max);
        CHECK(partial?completed>start:completed-start==137);
    }
    CHECK(member_cache&&lv_obj_get_child_count(home[1])==1);
    for(int i=0;i<3;i++)CHECK(thumbnail_cache[i]&&thumbnail_cache[i]->header.w==180);
    /* Upload descriptors are reused by both real services: replacing bytes must refresh the cached preview. */
    static uint16_t uploaded_pixels[360*360];
    lv_image_dsc_t image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=sizeof(uploaded_pixels),.data=(uint8_t*)uploaded_pixels};
    for(int pass=0;pass<6;pass++){
        for(int i=0;i<360*360;i++)uploaded_pixels[i]=pass%2?0x001f:0xf800;
        lv_image_cache_drop(&image);badge_ui_restore_photo(&image);CHECK(count()==3);
        const uint16_t *p=(const uint16_t *)(thumbnail_cache[2]->data+90*thumbnail_cache[2]->header.stride+90*2);
        CHECK(*p==(pass%2?0x001f:0xf800));
    }
    badge_ui_restore_photo(&badge_wallpaper);CHECK(count()==2&&uploaded==NULL);
    reject_snapshot=true;badge_ui_restore_photo(&image);
    CHECK(!thumbnail_cache[2]&&lv_image_get_src(tile_images[2])==&image&&lv_image_get_scale_x(tile_images[2])==128);
    reject_snapshot=false;badge_ui_restore_photo(&badge_wallpaper);CHECK(thumbnail_cache[2]);
    badge_ui_page(7,false);lv_refr_now(d);if(argc>1)capture(argv[1],2,9999);
    badge_ui_page(5,false);CHECK(!badge_ui_perf_enabled());
    lv_obj_send_event(perf_switch,LV_EVENT_SHORT_CLICKED,NULL);CHECK(badge_ui_perf_enabled());
    badge_ui_perf_text("UI 12.5 | LCD 12.5");lv_refr_now(d);
    CHECK(!lv_obj_has_flag(perf_label,LV_OBJ_FLAG_HIDDEN));if(argc>1)capture(argv[1],5,1);
    lv_display_add_event_cb(d,check_meter_area,LV_EVENT_INVALIDATE_AREA,NULL);
    watch_meter=true;badge_ui_perf_text("UI 0.0 | LCD 0.0");lv_refr_now(d);watch_meter=false;
    badge_ui_sleep(true);CHECK(lv_obj_has_flag(perf_label,LV_OBJ_FLAG_HIDDEN));
    badge_ui_sleep(false);CHECK(!lv_obj_has_flag(perf_label,LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(perf_switch,LV_EVENT_SHORT_CLICKED,NULL);CHECK(!badge_ui_perf_enabled()&&lv_obj_has_flag(perf_label,LV_OBJ_FLAG_HIDDEN));
    puts("Cache replacement/fallback and physical FPS switch/sleep visibility: PASS");
    return 0;
}
