#include "badge_ui.h"
#include "apps.h"
#include "ui_transition_cache.h"
#include "src/misc/cache/lv_cache.h"
#include "src/core/lv_obj_draw_private.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "display_perf.h"
#endif

/* 0 display, 1 functions, 2 wallpapers, 3 settings, 4 connections,
 * 5 appearance, 6 about, 7 member card, 8 control centre. */
static badge_state_t *s;
static void (*save_settings)(void),(*control)(int,int);
static lv_obj_t *root,*home[2],*photo,*drawers[2],*details[5];
static lv_obj_t *gallery,*drawer_content[2];
static lv_obj_t *tiles[3],*tile_images[3],*wall_title,*apply_button,*delete_button,*toast,*status;
static lv_obj_t *wifi_text,*ble_text,*voltage,*sliders[2],*brightness_text[2],*reduced;
static lv_obj_t *dimmer,*sleep_cover,*transition_input_layer;
static int transition_input_owner;
static void transition_input(bool active,int owner);
static lv_obj_t *perf_switch,*perf_label,*storage_total,*storage_free;
static bool perf_enabled;
bool badge_ui_perf_enabled(void){return perf_enabled;}
void badge_ui_perf_enable(bool enabled){
    if(perf_switch&&perf_enabled!=enabled)ui_transition_cache_invalidate_reason(perf_switch,"FPS_VISIBILITY");
    perf_enabled=enabled;
    if(perf_switch)badge_liquid_set(perf_switch,enabled,true);
    if(perf_label){if(enabled&&!badge_ui_is_asleep())lv_obj_remove_flag(perf_label,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(perf_label,LV_OBJ_FLAG_HIDDEN);}
}
void badge_ui_perf_text(const char *value){if(perf_label&&strcmp(lv_label_get_text(perf_label),value))lv_label_set_text(perf_label,value);}
bool badge_ui_perf_covers(const lv_area_t *area){
    if(!perf_enabled||!perf_label||!area)return false;
    lv_area_t meter;lv_obj_get_coords(perf_label,&meter);
    int ext=lv_obj_get_ext_draw_size(perf_label);lv_area_increase(&meter,ext,ext);
    return area->x1>=meter.x1&&area->x2<=meter.x2&&area->y1>=meter.y1&&area->y2<=meter.y2;
}
static const lv_image_dsc_t *uploaded;
static int uploaded_id=2,library_count,library_ids[31];
static const lv_image_dsc_t *library_thumbs[31];
static bool gallery_rebinding;
static int delete_confirm=-1;
static int item_id(int ordinal){return ordinal<2?ordinal:library_ids[ordinal-2];}
static int item_ordinal(int id){for(int i=0;i<library_count;i++)if(library_ids[i]==id)return i+2;return id<2&&id>=0?id:0;}
static lv_image_dsc_t thumbnail_cache[2];
static void *image_memory(size_t bytes){
#ifdef ESP_PLATFORM
    return heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}
static int current,selection,return_drawer=1,display_index;
#ifdef ESP_PLATFORM
static int cleanup_page=-1;
static bool cleanup_animate;
extern bool app_service_map_busy(void);
#endif
static bool sleeping,connected,wall_pending,applying;
static uint32_t toast_until,status_until,applied_until;
static lv_obj_t *slider_tail[2];
static float tail_value[2],tail_from[2],tail_target[2];
static uint32_t tail_at[2];
typedef struct {float value,target,velocity;bool moving;} motion_t;
static motion_t display_motion,drawer_motion,detail_motion;
static uint32_t last_tick;
static lv_obj_t *detail_from,*detail_to;
static int detail_direction;
static bool detail_fast;
static lv_obj_t *connection_background,*connection_viewport,*connection_content[4];
static bool connection_local;
static void connection_bitmap_end(void);
static void detail_cache_end(void){
    ui_transition_cache_end(detail_from);ui_transition_cache_end(detail_to);
}
static bool detail_cache_begin(lv_obj_t *from,lv_obj_t *to){
    bool a=ui_transition_cache_begin(from),b=ui_transition_cache_begin(to);
    return a||b;
}
typedef struct {bool down,moved;int owner,x,y,last_x,last_y;uint32_t time;float base,velocity;} drag_t;
static drag_t drag;
/* Shared by clickable controls; dragging a button must not activate it on release. */
void badge_ui_click_guard(lv_event_t *e){
    static lv_point_t origin;static bool consumed;
    lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_event_code_t code=lv_event_get_code(e);lv_point_t p;lv_indev_get_point(in,&p);
    if(code==LV_EVENT_PRESSED){origin=p;consumed=badge_ui_transition_active();}
    else if(code==LV_EVENT_PRESSING&&(abs(p.x-origin.x)>10||abs(p.y-origin.y)>10))consumed=true;
    else if(code==LV_EVENT_PRESS_LOST||code==LV_EVENT_INDEV_RESET)consumed=true;
    if((consumed||badge_ui_transition_active())&&(code==LV_EVENT_SHORT_CLICKED||code==LV_EVENT_CLICKED||code==LV_EVENT_LONG_PRESSED))lv_event_stop_processing(e);
}
static float clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static void shown(lv_obj_t *o,bool yes){if(!o)return;ui_transition_cache_show(o,yes);}
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int radius){
    lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
/* Shared clipped settings surfaces; business widgets retain their identity. */
static void connection_wrap(lv_obj_t *page,int index){
    lv_obj_update_layout(page);
    if(!connection_content[index]){connection_content[index]=box(connection_viewport,0,0,250,272,UI_BG,0);ui_transition_cache_register(connection_content[index],false);ui_transition_cache_identify(connection_content[index],20+index);}
    lv_obj_t *content=connection_content[index];
    ui_transition_cache_alias(content,page);ui_transition_cache_suspend(content,false,false);
    ui_transition_cache_keep(content,index!=1);
    while(lv_obj_get_child_count(page)){
        lv_obj_t *child=lv_obj_get_child(page,0);int x=lv_obj_get_x(child),y=lv_obj_get_y(child);
        lv_obj_set_parent(child,content);lv_obj_set_pos(child,x-50,y-28);
    }
}
static void connection_leave(void){
    if(!connection_local)return;
    ui_transition_cache_direct_end();
    connection_bitmap_end();
    for(int i=0;i<4;i++){
        lv_obj_t *content=connection_content[i];
        ui_transition_cache_suspend(content,true,false);lv_obj_set_pos(content,0,0);lv_obj_update_layout(content);
        while(lv_obj_get_child_count(content)){
            lv_obj_t *child=lv_obj_get_child(content,0);int x=lv_obj_get_x(child),y=lv_obj_get_y(child);
            lv_obj_set_parent(child,details[i+1]);lv_obj_set_pos(child,x+50,y+28);
        }
        shown(content,false);shown(details[i+1],current==i+3);
        ui_transition_cache_suspend(details[i+1],false,false);
    }
    shown(connection_background,false);connection_local=false;detail_from=detail_to=NULL;detail_motion.moving=false;
}
static void connection_enter(void){
    if(connection_local)return;
    detail_cache_end();
    if(detail_from){shown(detail_from,false);lv_obj_set_x(detail_from,0);}
    if(detail_to)lv_obj_set_x(detail_to,0);
    detail_from=detail_to=NULL;detail_motion.moving=false;
    if(!connection_background){
        connection_background=box(root,0,0,360,360,UI_BG,0);
        connection_viewport=box(connection_background,50,28,250,272,UI_BG,0);
    }
    shown(connection_background,true);lv_obj_move_foreground(connection_background);
    for(int i=0;i<4;i++){
        ui_transition_cache_suspend(details[i+1],true,current==i+3);
        connection_wrap(details[i+1],i);lv_obj_set_pos(connection_content[i],0,0);
        shown(connection_content[i],current==i+3);shown(details[i+1],false);
    }
    connection_local=true;
}
static void connection_bitmap_end(void){
    for(int i=0;i<4;i++)ui_transition_cache_end(connection_content[i]);
}
static bool connection_bitmap_begin(bool animate,int from,int to){
    if(!animate||s->reduced_motion)return false;
    bool a=ui_transition_cache_begin(connection_content[from-3]);
    bool b=ui_transition_cache_begin(connection_content[to-3]);
    return a||b;
}
static void navigation_prewarm(void){
    extern int badge_panels_cache_page(void),badge_apps_cache_page(void);
    int page=badge_panels_cache_page();if(page<0)page=badge_apps_cache_page();
    if(page<0)page=current==0?0:current==7?1:current==8?2:current==1?3:current==2?10:connection_local?20+current-3:11+current-3;
    static const unsigned home_plan[]={0,2,3,1},member_plan[]={1,0,2,3},control_plan[]={2,0,3,1};
    static const unsigned functions_plan[]={3,11,12,10,33,0};
    static const unsigned settings_plan[]={20,22,23,21,3,11},connection_plan[]={21,30,31,32,20,3,12};
    static const unsigned display_plan[]={22,20,23,21,3},about_plan[]={23,20,22,21,3};
    static const unsigned wifi_plan[]={30,21,31,32,20},ble_plan[]={31,21,30,32,20},phone_plan[]={32,21,30,31,20};
    static const unsigned apps_plan[]={33,3,0,11},wall_plan[]={10,3,0};
    const unsigned *ids=NULL;unsigned count=0;
#define PLAN(name) do{ids=name##_plan;count=sizeof(name##_plan)/sizeof(unsigned);}while(0)
    switch(page){
        case 0:PLAN(home);break;case 1:PLAN(member);break;case 2:PLAN(control);break;case 3:PLAN(functions);break;
        case 20:PLAN(settings);break;case 21:PLAN(connection);break;case 22:PLAN(display);break;case 23:PLAN(about);break;
        case 30:PLAN(wifi);break;case 31:PLAN(ble);break;case 32:PLAN(phone);break;case 33:PLAN(apps);break;case 10:PLAN(wall);break;
        default:break;
    }
#undef PLAN
    if(ids)ui_transition_cache_plan(ids,count);
}
static void connection_cache_poll(void){
    bool idle=!display_motion.moving&&!drawer_motion.moving&&!detail_motion.moving&&!drag.down;
    if(idle&&current>=3&&current<=6&&!connection_local){
        lv_obj_t *page=details[current-2];unsigned id=11+current-3;ui_transition_cache_plan(&id,1);
        if(!ui_transition_cache_ready(page)){ui_transition_cache_request(page);ui_transition_cache_poll(!s->reduced_motion);return;}
        connection_enter();
    }
    if(idle)navigation_prewarm();
    ui_transition_cache_poll(idle&&!s->reduced_motion&&lv_tick_get()-tail_at[0]>=80&&lv_tick_get()-tail_at[1]>=80);
}
void badge_ui_connection_cache_stats(char *out,size_t size){ui_transition_cache_stats(out,size);}
static lv_obj_t *text(lv_obj_t *p,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color){
    lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,value);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *center(lv_obj_t *p,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color){
    lv_obj_t *o=text(p,value,x,y,w,font,color);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *value,int x,int y,int w,int h,lv_event_cb_t cb,int arg){
    lv_obj_t *o=box(p,x,y,w,h,UI_SURFACE,18);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_SELECTED),LV_STATE_PRESSED);lv_obj_set_style_opa(o,110,LV_STATE_DISABLED);
    lv_obj_t *l=center(o,value,0,0,w,&font18,UI_TEXT);lv_obj_center(l);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)arg);
    return o;
}
/* Shared small controls: retarget from their current position, including reversals. */
static void liquid_x(void *obj,int32_t x){
    int width=22+(int)lroundf(2*sinf((x-3)*3.14159265f/20));
    if(lv_obj_get_x(obj)==x&&lv_obj_get_width(obj)==width)return;
    ui_transition_cache_invalidate_reason(obj,"SWITCH_POSITION");
    lv_obj_set_x(obj,x);lv_obj_set_width(obj,width);
}
void badge_liquid_set(lv_obj_t *o,bool on,bool reduced_motion){
    lv_obj_t *knob=lv_obj_get_child(o,0);int end=on?23:3;
    bool changed=lv_obj_has_state(o,LV_STATE_CHECKED)!=on;
    if(on)lv_obj_add_state(o,LV_STATE_CHECKED);else lv_obj_remove_state(o,LV_STATE_CHECKED);
    if(!changed&&!reduced_motion)return;
    if(changed)ui_transition_cache_invalidate(o);
    lv_anim_delete(knob,liquid_x);
    if(reduced_motion){liquid_x(knob,end);return;}
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,knob);lv_anim_set_exec_cb(&a,liquid_x);
    lv_anim_set_values(&a,lv_obj_get_x(knob),end);lv_anim_set_duration(&a,160);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
}
static void liquid_click(lv_event_t *e){
    lv_obj_t *o=lv_event_get_target_obj(e);badge_liquid_set(o,!lv_obj_has_state(o,LV_STATE_CHECKED),s->reduced_motion);
    lv_obj_send_event(o,LV_EVENT_VALUE_CHANGED,NULL);
}
lv_obj_t *badge_liquid_create(lv_obj_t *p,int x,int y,lv_event_cb_t cb){
    lv_obj_t *o=box(p,x,y,48,28,UI_LINE,14);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_ext_click_area(o,8);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_ACCENT),LV_STATE_CHECKED);box(o,3,3,22,22,0xFFFFFF,11);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);
    lv_obj_add_event_cb(o,liquid_click,LV_EVENT_SHORT_CLICKED,NULL);lv_obj_add_event_cb(o,cb,LV_EVENT_VALUE_CHANGED,NULL);return o;
}
static void opacity(void *o,int32_t v){lv_obj_set_style_opa(o,v,0);}
static void notify_label(lv_obj_t *label,const char *value){
    if(!strcmp(lv_label_get_text(label),value))return;
    lv_label_set_text(label,value);lv_anim_delete(label,opacity);
    if(s->reduced_motion){opacity(label,255);return;}
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,label);lv_anim_set_exec_cb(&a,opacity);
    lv_anim_set_values(&a,100,255);lv_anim_set_duration(&a,180);lv_anim_start(&a);
}
static void primary(lv_obj_t *o){lv_obj_set_style_bg_color(o,lv_color_hex(UI_ACCENT),0);lv_obj_set_style_bg_color(o,lv_color_hex(0x25496F),LV_STATE_PRESSED);lv_obj_set_style_text_color(lv_obj_get_child(o,0),lv_color_white(),0);}
static const lv_image_dsc_t *wallpaper(int i){
    if(i==uploaded_id&&uploaded)return uploaded;
    i=i==1?1:0;
    return i?&badge_ribbons:&badge_wallpaper;
}
static void set_tile_source(int i,const lv_image_dsc_t *src){
    uint16_t *data=image_memory(180u*180u*2u);if(!data)return;
    for(int y=0;y<180;y++)for(int x=0;x<180;x++)data[y*180+x]=((const uint16_t*)(src->data+y*2*src->header.stride))[x*2];
    thumbnail_cache[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=180,.h=180,.stride=360},.data_size=64800,.data=(const uint8_t*)data};
}
static int count(void){return library_count+2;}
static void target(motion_t *m,float value,bool animate){m->target=value;m->moving=animate&&!s->reduced_motion;if(!m->moving){m->value=value;m->velocity=0;}}
static void step(motion_t *m,float dt){
    if(!m->moving)return;
    /* Exact critically damped solution: keeps position/velocity through reversals. */
    const float omega=32;float x=m->value-m->target,k=m->velocity+omega*x,e=expf(-omega*dt);
    m->value=m->target+(x+k*dt)*e;m->velocity=(m->velocity-omega*k*dt)*e;
    if(fabsf(m->value-m->target)<.002f&&fabsf(m->velocity)<.03f){m->value=m->target;m->velocity=0;m->moving=false;}
}
static void preview_label(void){
    int id=item_id(selection);
    static uint64_t old=UINT64_MAX;
    uint64_t key=(unsigned)id|((uint64_t)(unsigned)s->wallpaper_index<<8)|((uint64_t)applying<<16)|((uint64_t)wall_pending<<17)|((uint64_t)(unsigned)(delete_confirm+1)<<24);
    if(key!=old){ui_transition_cache_invalidate(details[0]);old=key;}
    if(id>=2)lv_label_set_text_fmt(wall_title,"壁纸 %02d",id-1);else lv_label_set_text(wall_title,id==1?"流光":"晴日");
    notify_label(lv_obj_get_child(apply_button,0),applying?"正在应用…":id==s->wallpaper_index?"已应用":"应用");
    if(applying||wall_pending||id==s->wallpaper_index)lv_obj_add_state(apply_button,LV_STATE_DISABLED);else lv_obj_remove_state(apply_button,LV_STATE_DISABLED);
    if(delete_button){
        lv_label_set_text(lv_obj_get_child(delete_button,0),delete_confirm==id?"确认删除":"删除");
        if(id<2||applying||wall_pending)lv_obj_add_state(delete_button,LV_STATE_DISABLED);else lv_obj_remove_state(delete_button,LV_STATE_DISABLED);
    }
}
static void gallery_layout(void){
    if(!gallery||!tile_images[2])return;
    if(lv_obj_get_scroll_x(gallery)!=240)ui_transition_cache_invalidate(details[0]);
    gallery_rebinding=true;
    for(int i=0;i<3;i++){
        int ordinal=(selection+i-1+count())%count();
        const lv_image_dsc_t *src=ordinal<2?&thumbnail_cache[ordinal]:library_thumbs[ordinal-2];
        if(src&&!src->data)src=NULL;
        const void *image=src?src:wallpaper(0);
        if(lv_image_get_src(tile_images[i])!=image)ui_transition_cache_invalidate(details[0]);
        lv_image_set_src(tile_images[i],image);lv_image_set_scale(tile_images[i],src?256:128);
    }
    lv_obj_update_layout(gallery);lv_obj_scroll_to_x(gallery,240,LV_ANIM_OFF);gallery_rebinding=false;
}
static void gallery_scrolled(lv_event_t *e){
    if(gallery_rebinding)return;
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_INDEV_RESET){lv_obj_stop_scroll_anim(gallery);gallery_layout();return;}
    if(code!=LV_EVENT_SCROLL_END)return;
    int offset=lv_obj_get_scroll_x(gallery),direction=offset<120?-1:offset>360?1:0;
    if(direction){selection=(selection+direction+count())%count();applied_until=0;delete_confirm=-1;gallery_layout();preview_label();}
}
unsigned badge_ui_thumbnail_window(int ids[3]){
    if(current!=2)return 0;
    unsigned n=0;for(int i=0;i<3;i++){int id=item_id((selection+i-1+count())%count());if(id>=2)ids[n++]=id;}return n;
}
void badge_ui_thumbnails_changed(void){
    /* Rebind before freeing an evicted descriptor's pixels; do not reset a drag. */
    if(!tile_images[2])return;
    for(int i=0;i<3;i++){
        int ordinal=(selection+i-1+count())%count();
        const lv_image_dsc_t *src=ordinal<2?&thumbnail_cache[ordinal]:library_thumbs[ordinal-2];
        if(src&&!src->data)src=NULL;
        lv_image_set_src(tile_images[i],src?src:wallpaper(0));lv_image_set_scale(tile_images[i],src?256:128);
    }
    ui_transition_cache_invalidate(details[0]);
}
/* One scene description for all existing slide/drawer routes. LVGL retains
 * final object geometry; the compositor receives absolute LCD positions only. */
static bool direct_rejected;
static bool direct_external;
static uint16_t direct_background(void){uint16_t c=lv_color_to_u16(lv_color_hex(UI_BG));return (uint16_t)((c>>8)|(c<<8));}
static unsigned direct_base(lv_obj_t **objects,int *xy){
    unsigned n=0;
    if(connection_local&&current>=3&&current<=6){objects[n]=connection_content[current-3];xy[n*2]=50;xy[n*2+1]=28;return 1;}
    if(current>=2&&current<=6){objects[0]=details[current-2];xy[0]=xy[1]=0;return 1;}
    objects[n]=home[display_index?1:0];xy[n*2]=xy[n*2+1]=0;n++;
    if(drawer_motion.value!=0){int i=drawer_motion.value>0;objects[n]=drawer_content[i];xy[n*2]=0;
        xy[n*2+1]=(int)lroundf(i?360*(1-drawer_motion.value):-360*(1+drawer_motion.value));n++;}
    return n;
}
bool badge_ui_direct_panel(lv_obj_t *panel,int x){
    if(sleeping)return false;
    lv_obj_t *objects[4];int xy[8];unsigned n=direct_base(objects,xy);
    objects[n]=panel;xy[n*2]=x;xy[n*2+1]=0;n++;
    for(unsigned i=0;i<n;i++)ui_transition_cache_request(objects[i]);
    bool ok=ui_transition_cache_direct(objects,xy,n,0,0,360,360,direct_background());
    direct_external=ok;transition_input(ok,99);return ok;
}
void badge_ui_direct_panel_end(void){if(direct_external){ui_transition_cache_direct_end();direct_external=false;transition_input(false,99);}}
static bool direct_layout(void){
    if(direct_external)return true;
    bool moving=detail_motion.moving||drawer_motion.moving||display_motion.moving||(drag.down&&drag.moved&&drag.owner);
    if(!moving||s->reduced_motion){ui_transition_cache_direct_end();direct_rejected=false;return false;}
    if(direct_rejected)return false;
    lv_obj_t *objects[4];int xy[8];unsigned n=0;int vx=0,vy=0,w=360,h=360;
    if(detail_motion.moving){
        if(connection_local){vx=50;vy=28;w=250;h=272;}
        else {
            objects[n]=home[display_index?1:0];xy[n*2]=xy[n*2+1]=0;n++;
            if(drawer_motion.value!=0){int i=drawer_motion.value>0;objects[n]=drawer_content[i];xy[n*2]=0;
                xy[n*2+1]=(int)lroundf(i?360*(1-drawer_motion.value):-360*(1+drawer_motion.value));n++;}
        }
        if(detail_from){objects[n]=detail_from;xy[n*2]=vx-(int)lroundf(detail_direction*w*detail_motion.value);xy[n*2+1]=vy;n++;}
        if(detail_to){objects[n]=detail_to;xy[n*2]=vx+(int)lroundf(detail_direction*w*(1-detail_motion.value));xy[n*2+1]=vy;n++;}
    }else if((drawer_motion.moving&&(fabsf(drawer_motion.value-drawer_motion.target)>.0001f||fabsf(drawer_motion.velocity)>.0001f))||(drag.down&&drag.moved&&drag.owner!=3)){
        float d=clamp(drawer_motion.value,-1,1);int i=d>0||(d==0&&drawer_motion.target>0);
        objects[0]=home[display_index?1:0];xy[0]=xy[1]=0;
        objects[1]=drawer_content[i];xy[2]=0;xy[3]=(int)lroundf(i?360*(1-d):-360*(1+d));n=2;
    }else {
        float v=clamp(display_motion.value,0,1);objects[0]=home[0];objects[1]=home[1];
        xy[0]=(int)lroundf(-360*v);xy[1]=0;xy[2]=(int)lroundf(360*(1-v));xy[3]=0;n=2;
    }
    for(unsigned i=0;i<n;i++)ui_transition_cache_request(objects[i]);
    if(ui_transition_cache_direct(objects,xy,n,vx,vy,w,h,direct_background()))return true;
    direct_rejected=true;return false;
}
static void layout(void){
    if(direct_layout()){
        int owner=direct_external||detail_motion.moving?99:drawer_motion.value||drawer_motion.target?(drawer_motion.value<0||drawer_motion.target<0?-1:1):0;
        transition_input(true,owner);return;
    }
    transition_input(false,99);
    float x=clamp(display_motion.value,0,1);lv_obj_set_x(home[0],(int)lroundf(-360*x));lv_obj_set_x(home[1],(int)lroundf(360*(1-x)));
    float d=clamp(drawer_motion.value,-1,1);shown(drawers[0],d<0);shown(drawers[1],d>0);
    lv_obj_set_y(drawers[0],(int)lroundf(-360*(1+d)));lv_obj_set_y(drawers[1],(int)lroundf(360*(1-d)));
    if(detail_from)lv_obj_set_x(detail_from,(int)lroundf(-detail_direction*(connection_local?250:360)*detail_motion.value));
    if(detail_to)lv_obj_set_x(detail_to,(int)lroundf(detail_direction*(connection_local?250:360)*(1-detail_motion.value)));
    for(int i=0;i<2;i++){
        bool moving=drawer_motion.moving||(drag.down&&drag.moved&&drag.owner!=3);
        if(moving&&!s->reduced_motion&&((i==0&&d<0)||(i==1&&d>0)))ui_transition_cache_begin(drawer_content[i]);
        else if(!moving)ui_transition_cache_end(drawer_content[i]);
        ui_transition_cache_position(drawer_content[i]);
    }
    ui_transition_cache_position(detail_from);ui_transition_cache_position(detail_to);
    if(!detail_motion.moving&&detail_motion.value>=1){detail_cache_end();connection_bitmap_end();if(detail_from){shown(detail_from,false);lv_obj_set_x(detail_from,0);}detail_from=NULL;detail_to=NULL;}
}
static void tick(lv_timer_t *t){
#ifdef ESP_PLATFORM
    if(cleanup_page>=0&&!app_service_map_busy()){
        int page=cleanup_page;cleanup_page=-1;badge_ui_page(page,cleanup_animate);
    }
#endif
    (void)t;uint32_t now=lv_tick_get();float dt=clamp((now-last_tick)/1000.f,0,.2f);last_tick=now;if(sleeping)return;
    bool move=display_motion.moving||drawer_motion.moving||detail_motion.moving;
    step(&display_motion,dt);step(&drawer_motion,dt);step(&detail_motion,detail_fast?dt*1.5f:dt);if(move)layout();
    for(int i=0;i<2;i++){
        float value=lv_slider_get_value(sliders[i]);
        if(value!=tail_target[i]){tail_from[i]=clamp(tail_value[i],value-4*90.f/204,value+4*90.f/204);tail_target[i]=value;tail_at[i]=now;}
        float q=clamp((now-tail_at[i])/80.f,0,1);
        tail_value[i]=s->reduced_motion?value:value+(tail_from[i]-value)*(1-q)*(1-q)*(1-q);
        int x=(int)lroundf(78+(tail_value[i]-10)*204/90.f)-3;
        if(lv_obj_get_x(slider_tail[i])!=x){ui_transition_cache_invalidate(slider_tail[i]);lv_obj_set_x(slider_tail[i],x);}
    }
    if(applied_until&&(int32_t)(now-applied_until)>=0){applied_until=0;if(current==2)badge_ui_page(0,true);}
    if(toast_until&&(int32_t)(now-toast_until)>=0){toast_until=0;shown(toast,false);}
    if(status_until&&(int32_t)(now-status_until)>=0&&!drag.down){status_until=0;shown(status,false);}
    connection_cache_poll();
}
void badge_ui_notice(const char *m){if(!toast)return;lv_label_set_text(lv_obj_get_child(toast,0),m);shown(toast,true);lv_obj_move_foreground(toast);toast_until=lv_tick_get()+2200;}
static void commit_gallery(int to){
    applied_until=0;int direction=to>selection?1:to<selection?-1:0;
    if(!direction)return;
    lv_obj_scroll_to_x(gallery,240+direction*240,s->reduced_motion?LV_ANIM_OFF:LV_ANIM_ON);
}
static void drag_event(lv_event_t *e){

    if(lv_event_get_code(e)==LV_EVENT_PRESS_LOST||lv_event_get_code(e)==LV_EVENT_INDEV_RESET){
        if(drag.down){
            drag.down=false;drag.moved=true;display_motion.velocity=drawer_motion.velocity=0;
            target(&display_motion,display_index,true);target(&drawer_motion,drawer_motion.target,true);
            current=drawer_motion.target<0?8:drawer_motion.target>0?1:display_index?7:0;
        }
        return;
    }
    lv_indev_t *in=lv_indev_active();if(!in)return;lv_point_t p;lv_indev_get_point(in,&p);
    int owner=lv_event_get_target_obj(e)==transition_input_layer?transition_input_owner:(int)(intptr_t)lv_event_get_user_data(e);
    if(owner==99)return;
    lv_event_code_t code=lv_event_get_code(e);uint32_t now=lv_tick_get();
    if(code==LV_EVENT_PRESSED){
        drag=(drag_t){.down=true,.owner=owner,.x=p.x,.y=p.y,.last_x=p.x,.last_y=p.y,.time=now};
        if(owner==1||owner==-1){drawer_motion.moving=false;drag.base=drawer_motion.value;}
        else {display_motion.moving=false;drawer_motion.moving=false;drag.base=display_motion.value;}
    }else if(code==LV_EVENT_PRESSING&&drag.down){
        int dx=p.x-drag.x,dy=p.y-drag.y;
        if((!drag.moved||drag.owner==0)&&(abs(dx)>10||abs(dy)>10)){
            drag.moved=true;shown(status,false);
            if(owner==0){
                if(abs(dx)<abs(dy)*1.3f&&abs(dy)<abs(dx)*1.3f&&abs(dx)<20&&abs(dy)<20)return;
                if(abs(dx)>abs(dy)){drag.owner=3;drag.base=display_motion.value;}
                else {drag.owner=dy>0?-1:1;drag.base=drawer_motion.value;return_drawer=drag.owner;current=drag.owner<0?8:1;}}
        }
        uint32_t elapsed=now-drag.time;
        if(drag.moved&&drag.owner!=0){
            if(drag.owner==3){display_motion.value=clamp(drag.base-dx/360.f,0,1);layout();}
            else {drawer_motion.value=clamp(drag.base-dy/360.f,drag.owner<0?-1:0,drag.owner<0?0:1);layout();}
            if(elapsed)drag.velocity=.6f*drag.velocity+.4f*((drag.owner==3?-(p.x-drag.last_x)/360.f:-(p.y-drag.last_y)/360.f)*1000.f/elapsed);
        }
        drag.last_x=p.x;drag.last_y=p.y;drag.time=now;
    }else if(code==LV_EVENT_RELEASED&&drag.down){
        drag.down=false;if(now-drag.time>100||code==LV_EVENT_PRESS_LOST)drag.velocity=0;
        if(drag.moved&&drag.owner!=0){
            if(drag.owner==3){display_motion.velocity=clamp(drag.velocity,-4,4);display_index=display_motion.value+drag.velocity*.12f>.5f;target(&display_motion,display_index,true);current=display_index?7:0;}
            else {drawer_motion.velocity=clamp(drag.velocity,-4,4);float projected=drawer_motion.value+drag.velocity*.12f;float to=drag.owner<0?(projected<-.35f?-1:0):(projected>.35f?1:0);target(&drawer_motion,to,true);current=to<0?8:to>0?1:display_index?7:0;}
            layout();
        }else {
            target(&display_motion,display_motion.target,true);target(&drawer_motion,drawer_motion.target,true);
            if(!drag.moved&&owner==0&&code==LV_EVENT_RELEASED){shown(status,lv_obj_has_flag(status,LV_OBJ_FLAG_HIDDEN));status_until=lv_tick_get()+3000;}
        }
    }
}
/* The direct frame has no LVGL hit-test geometry. Keep ordinary controls
 * behind one transparent input layer; existing drawer/home gestures can take over. */
static void transition_input(bool active,int owner){
    if(!transition_input_layer)return;
    transition_input_owner=owner;
    if(active){lv_obj_remove_flag(transition_input_layer,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(transition_input_layer);}
    else lv_obj_add_flag(transition_input_layer,LV_OBJ_FLAG_HIDDEN);
}
static void draggable(lv_obj_t *o,int owner){lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);lv_obj_add_event_cb(o,drag_event,LV_EVENT_ALL,(void*)(intptr_t)owner);}
static void go(lv_event_t *e){badge_ui_page((int)(intptr_t)lv_event_get_user_data(e),true);}
static void header(lv_obj_t *p,const char *title,int parent){lv_obj_t *b=button(p,"‹",85,36,44,44,go,parent);lv_obj_set_style_bg_opa(b,0,0);lv_obj_set_style_text_font(lv_obj_get_child(b,0),&font24,0);center(p,title,124,31,145,&font24,UI_TEXT);}
static void delete_clicked(lv_event_t *e){
    (void)e;int id=item_id(selection);if(id<2)return;
    if(delete_confirm!=id){delete_confirm=id;preview_label();return;}
    delete_confirm=-1;badge_ui_request(BADGE_DELETE,id);
}
static void apply(lv_event_t *e){(void)e;if(wall_pending)return;applied_until=0;applying=true;preview_label();badge_ui_request(BADGE_APPLY,item_id(selection));}
static void wifi(lv_event_t *e){(void)e;badge_wifi_open();}
static void ble(lv_event_t *e){(void)e;badge_ble_open();}
static void apps(lv_event_t *e){(void)e;badge_apps_open();}
static void manage(lv_event_t *e){(void)e;badge_wallpaper_open();}
static void sleep_clicked(lv_event_t *e){(void)e;badge_ui_request(BADGE_SLEEP,1);}
static void wake(lv_event_t *e){(void)e;badge_ui_request(BADGE_SLEEP,0);}
static int brightness_preview=-1;
int badge_ui_brightness(void){return brightness_preview>=0?brightness_preview:s->brightness;}
static void brightness_changed(lv_event_t *e){
    lv_obj_t *o=lv_event_get_target_obj(e);int v=lv_slider_get_value(o);
    if(brightness_preview!=v){ui_transition_cache_invalidate_reason(sliders[0],"BRIGHTNESS");ui_transition_cache_invalidate_reason(sliders[1],"BRIGHTNESS");}
    if(lv_obj_has_state(o,LV_STATE_PRESSED))brightness_preview=v;
    for(int i=0;i<2;i++)lv_label_set_text_fmt(brightness_text[i],"%d%%",v);
}
static void brightness_released(lv_event_t *e){brightness_preview=-1;badge_ui_request(BADGE_BRIGHTNESS,lv_slider_get_value(lv_event_get_target_obj(e)));}
static void brightness_cancelled(lv_event_t *e){(void)e;brightness_preview=-1;badge_ui_refresh();}
static void reduced_changed(lv_event_t *e){badge_ui_request(BADGE_REDUCED,lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED));}
static void perf_changed(lv_event_t *e){badge_ui_perf_enable(lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED));}
static void wifi_toggle(lv_event_t *e){(void)e;extern void badge_wifi_request_enable(bool);badge_wifi_request_enable(!s->wifi_enabled);}
static void ble_toggle(lv_event_t *e){(void)e;extern void badge_ble_request_toggle(void);badge_ble_request_toggle();}
static void wifi_long(lv_event_t *e){(void)e;badge_wifi_open();}
static void ble_long(lv_event_t *e){(void)e;badge_ble_open();}
static lv_obj_t *slider(lv_obj_t *p,int which,int y){
    text(p,"亮度",65,y-34,160,&font18,UI_TEXT);brightness_text[which]=text(p,"",235,y-33,66,&font14,UI_MUTED);
    slider_tail[which]=box(p,78,y+17,6,3,UI_ACCENT,2);tail_value[which]=tail_from[which]=tail_target[which]=s->brightness;
    lv_obj_t *o=lv_slider_create(p);lv_obj_set_pos(o,78,y);lv_obj_set_size(o,204,8);lv_obj_set_ext_click_area(o,20);
    lv_slider_set_range(o,10,100);lv_obj_remove_flag(o,LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_LINE),LV_PART_MAIN);lv_obj_set_style_bg_opa(o,255,LV_PART_MAIN);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_ACCENT),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(o,255,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o,lv_color_white(),LV_PART_KNOB);lv_obj_set_style_bg_opa(o,255,LV_PART_KNOB);
    lv_obj_set_style_pad_all(o,9,LV_PART_KNOB);lv_obj_set_style_radius(o,16,LV_PART_KNOB);lv_obj_set_style_radius(o,8,LV_PART_MAIN);lv_obj_set_style_radius(o,8,LV_PART_INDICATOR);
    lv_obj_add_event_cb(o,brightness_changed,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(o,brightness_released,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(o,brightness_cancelled,LV_EVENT_PRESS_LOST,NULL);return o;
}
void badge_ui_create(lv_obj_t *parent,badge_state_t *state,void (*save)(void)){
    s=state;save_settings=save;current=0;display_index=0;selection=0;connected=!state->live;
    root=box(parent,0,0,360,360,0,0);
#ifndef ESP_PLATFORM
    lv_obj_set_style_radius(root,180,0);lv_obj_set_style_clip_corner(root,true,0); /* Physical round glass already masks invisible corners. */
#endif
    for(int i=0;i<2;i++){home[i]=box(root,i*360,0,360,360,0xE7EDF5,0);draggable(home[i],0);}
    photo=lv_image_create(home[0]);lv_image_set_src(photo,wallpaper(selection));lv_obj_remove_flag(photo,LV_OBJ_FLAG_CLICKABLE);
    center(home[1],"NBTCA",116,43,128,&font18,UI_ACCENT);
    lv_obj_t *card=box(home[1],31,96,298,181,0x124689,25);lv_obj_set_style_clip_corner(card,true,0);
    for(int i=0;i<3;i++){lv_obj_t *arc=box(card,183+i*17,-33+i*8,180,235,0x124689,110);lv_obj_set_style_bg_opa(arc,0,0);lv_obj_set_style_border_color(arc,lv_color_hex(0x5685C3),0);lv_obj_set_style_border_width(arc,1,0);}
    text(card,"NBTCA / MEMBER",21,17,209,&font14,0xBFD5F4);text(card,"Egger",19,56,183,&font56,0xFFFFFF);text(card,"计算机协会",23,137,174,&font18,0xE0ECFF);
    lv_obj_t *mark=lv_image_create(card);lv_image_set_src(mark,&badge_logo);lv_image_set_pivot(mark,0,0);lv_image_set_scale(mark,126);lv_obj_set_pos(mark,211,72);lv_obj_remove_flag(mark,LV_OBJ_FLAG_CLICKABLE);
    center(home[1],"COMPUTER ASSOCIATION",74,293,212,&font14,UI_MUTED);
    /* Keep real widgets: the shared transition cache owns the only raster copy. */
    status=box(root,80,246,200,80,UI_SURFACE,24);button(status,"控制",10,18,85,44,go,8);button(status,"功能",105,18,85,44,go,1);shown(status,false);
    for(int i=0;i<2;i++){drawers[i]=box(root,0,i?360:-360,360,360,UI_BG,0);draggable(drawers[i],i?1:-1);shown(drawers[i],false);}
    lv_obj_t *p=drawers[0];center(p,"控制中心",104,32,152,&font24,UI_TEXT);voltage=center(p,"-- V",109,69,142,&font14,UI_MUTED);
    lv_obj_t *w=button(p,"Wi-Fi",48,102,127,76,wifi_toggle,0);wifi_text=lv_obj_get_child(w,0);primary(w);lv_obj_add_event_cb(w,wifi_long,LV_EVENT_LONG_PRESSED,NULL);
    lv_obj_t *b=button(p,"蓝牙",185,102,127,76,ble_toggle,0);ble_text=lv_obj_get_child(b,0);primary(b);lv_obj_add_event_cb(b,ble_long,LV_EVENT_LONG_PRESSED,NULL);
    sliders[0]=slider(p,0,236);button(p,"手机管理",64,267,112,44,manage,0);button(p,"息屏",184,267,112,44,sleep_clicked,0);box(p,154,331,52,4,0xAAB6C3,2);
    p=drawers[1];box(p,154,23,52,4,0xAAB6C3,2);center(p,"功能",82,46,196,&font36,UI_TEXT);
    lv_obj_t *f=button(p,"",47,113,266,91,go,2),*thumb=box(f,12,12,67,67,UI_BG,16);lv_obj_set_style_clip_corner(thumb,true,0);
    lv_obj_t *im=lv_image_create(thumb);lv_image_set_src(im,&badge_wallpaper);lv_image_set_pivot(im,0,0);lv_image_set_scale(im,48);
    text(f,"壁纸",98,14,137,&font24,UI_TEXT);text(f,"选一张喜欢的",98,52,151,&font14,UI_MUTED);
    button(p,"连接",56,212,119,48,go,4);button(p,"设置",185,212,119,48,go,3);button(p,"应用",75,271,210,48,apps,0);
    for(int i=0;i<5;i++){details[i]=box(root,0,0,360,360,UI_BG,0);shown(details[i],false);}
    p=details[0];header(p,"壁纸",1);
    gallery=box(p,60,76,240,180,UI_BG,0);
    lv_obj_add_flag(gallery,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_remove_flag(gallery,LV_OBJ_FLAG_SCROLL_CHAIN|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(gallery,LV_DIR_HOR);lv_obj_set_scroll_snap_x(gallery,LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(gallery,LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(gallery,gallery_scrolled,LV_EVENT_ALL,NULL);
    for(int i=0;i<3;i++){
        tiles[i]=box(gallery,i*240,0,240,180,UI_BG,0);lv_obj_add_flag(tiles[i],LV_OBJ_FLAG_SNAPPABLE);
        tile_images[i]=lv_image_create(tiles[i]);lv_obj_set_pos(tile_images[i],30,0);lv_image_set_pivot(tile_images[i],0,0);
        if(i<2)set_tile_source(i,wallpaper(i));
    }
    wall_title=center(p,"",77,258,206,&font14,UI_MUTED);
    apply_button=button(p,"应用",78,281,96,44,apply,0);primary(apply_button);
    delete_button=button(p,"删除",186,281,96,44,delete_clicked,0);
    p=details[1];header(p,"设置",1);button(p,"显示",61,100,238,58,go,5);button(p,"设备信息",61,171,238,58,go,6);button(p,"立即息屏",85,247,190,48,sleep_clicked,0);
    p=details[2];header(p,"连接",1);button(p,"Wi-Fi",51,103,124,99,wifi,0);button(p,"蓝牙",185,103,124,99,ble,0);primary(button(p,"手机管理",78,228,204,52,manage,0));
    p=details[3];header(p,"显示",3);sliders[1]=slider(p,1,153);text(p,"减少动态",65,211,167,&font18,UI_TEXT);
    reduced=badge_liquid_create(p,241,210,reduced_changed);
    text(p,"FPS",65,265,167,&font18,UI_TEXT);perf_switch=badge_liquid_create(p,241,264,perf_changed);
    p=details[4];header(p,"设备信息",3);center(p,"CABadge",66,119,228,&font36,UI_TEXT);center(p,"计算机协会",80,183,200,&font18,UI_MUTED);center(p,UI_VERSION,72,234,216,&font14,UI_ACCENT);
    storage_total=center(p,"Flash -- MB",58,256,244,&font14,UI_MUTED);
    storage_free=center(p,"壁纸可用 --",58,278,244,&font14,UI_MUTED);
    badge_wifi_create(root,s);badge_ble_create(root,s);badge_wallpaper_create(root);badge_apps_create(root);
    dimmer=box(root,0,0,360,360,0,180);sleep_cover=box(root,0,0,360,360,0,180);lv_obj_add_flag(sleep_cover,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(sleep_cover,wake,LV_EVENT_SHORT_CLICKED,NULL);shown(sleep_cover,false);
#ifdef ESP_PLATFORM
    shown(dimmer,false); /* Real LED PWM controls brightness; no extra full-screen alpha pass. */
#endif
    toast=box(root,75,265,210,55,UI_TEXT,19);center(toast,"",12,14,186,&font14,0xFFFFFF);shown(toast,false);
    perf_label=center(lv_layer_top(),"UI -- | LCD --",88,312,184,&font14,0xFFFFFF);
    lv_obj_set_style_bg_color(perf_label,lv_color_hex(UI_TEXT),0);lv_obj_set_style_bg_opa(perf_label,255,0);
    lv_obj_set_style_radius(perf_label,5,0);badge_ui_perf_enable(false);
    for(int j=0;j<2;j++){
        drawer_content[j]=box(drawers[j],0,0,360,360,UI_BG,0);
        while(lv_obj_get_child(drawers[j],0)!=drawer_content[j])lv_obj_set_parent(lv_obj_get_child(drawers[j],0),drawer_content[j]);
        ui_transition_cache_register(drawer_content[j],true);ui_transition_cache_identify(drawer_content[j],2+j);ui_transition_cache_keep(drawer_content[j],true);
    }
    for(int j=0;j<5;j++){ui_transition_cache_register(details[j],false);ui_transition_cache_identify(details[j],10+j);}
    for(int j=0;j<2;j++){ui_transition_cache_register(home[j],true);ui_transition_cache_identify(home[j],j);ui_transition_cache_keep(home[j],j==0);}
    transition_input_layer=box(lv_layer_top(),0,0,360,360,0,0);
    lv_obj_set_style_bg_opa(transition_input_layer,LV_OPA_TRANSP,0);
    lv_obj_add_flag(transition_input_layer,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(transition_input_layer,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(transition_input_layer,drag_event,LV_EVENT_ALL,NULL);
    transition_input(false,99);
    last_tick=lv_tick_get();lv_timer_create(tick,10,NULL);badge_ui_refresh();gallery_layout();preview_label();layout();
}
static lv_obj_t *page_obj(int i){return i>=2&&i<=6?details[i-2]:NULL;}
int badge_ui_current_page(void){return current;}
int badge_ui_selected_wallpaper(void){return s->wallpaper_index;}
bool badge_ui_transition_active(void){return detail_motion.moving||drawer_motion.moving||display_motion.moving||ui_transition_cache_active();}
void badge_ui_page(int i,bool animate){
#ifdef ESP_PLATFORM
    if(i>=0&&i<=12&&app_service_map_busy()){
        badge_apps_hide();cleanup_page=i;cleanup_animate=animate;return;
    }
    cleanup_page=-1;
#endif
    brightness_preview=-1;
    if(i>=9&&i<=12){badge_ui_page(1,false);badge_apps_launch(i-9);return;}
    if(i<0||i>8)return;
    badge_apps_hide();
    badge_wifi_hide();badge_ble_hide();badge_wallpaper_hide();shown(status,false);
    if(i==current)return;
    ui_transition_cache_direct_end();transition_input(false,99);direct_rejected=false;
    #ifdef ESP_PLATFORM
    display_perf_transition_begin(current,i,s->reduced_motion);
    #endif
    detail_fast=(current==3||current==5||current==6)&&(i==3||i==5||i==6);
    bool connection_pair=current>=3&&current<=6&&i>=3&&i<=6;
    if(!connection_pair)connection_leave();
    if(connection_pair){
        connection_enter();
        connection_bitmap_begin(animate,current,i);
        lv_obj_t *from=connection_content[current-3],*to=connection_content[i-3];
        if(detail_motion.moving&&to==detail_from){
            lv_obj_t *swap=detail_from;detail_from=detail_to;detail_to=swap;
            detail_direction=-detail_direction;detail_motion.value=1-detail_motion.value;detail_motion.velocity=-detail_motion.velocity;
        }else{
            detail_from=from;detail_to=to;detail_direction=i<current?-1:1;
            detail_motion.value=0;detail_motion.velocity=0;shown(to,true);
        }
        drag.down=false;target(&detail_motion,1,animate);current=i;layout();
#ifdef ESP_PLATFORM
        display_perf_transition_call_done();
#endif
        return;
    }
    drag.down=false;detail_cache_end();
    lv_obj_t *from=page_obj(current),*to=page_obj(i);
    if(detail_motion.moving&&(to==detail_from||(!to&&!detail_from))){
        lv_obj_t *swap=detail_from;detail_from=detail_to;detail_to=swap;
        detail_direction=-detail_direction;detail_motion.value=1-detail_motion.value;detail_motion.velocity=-detail_motion.velocity;
        target(&detail_motion,1,animate);current=i;layout();
#ifdef ESP_PLATFORM
        display_perf_transition_call_done();
#endif
        return;
    }
    if(detail_from){shown(detail_from,false);lv_obj_set_x(detail_from,0);detail_from=NULL;}
    if(detail_to){lv_obj_set_x(detail_to,0);detail_to=NULL;}detail_motion.moving=false;
    if(i==2&&current!=2){selection=item_ordinal(s->wallpaper_index);lv_obj_stop_scroll_anim(gallery);gallery_layout();preview_label();}
    if(to){
        if(current==1||current==8)return_drawer=current==8?-1:1;
        detail_from=from;detail_to=to;detail_direction=i<current?-1:1;shown(to,true);lv_obj_move_foreground(to);lv_obj_move_foreground(dimmer);lv_obj_move_foreground(sleep_cover);
        if(animate&&!s->reduced_motion)detail_cache_begin(from,to);
        detail_motion.value=0;detail_motion.velocity=0;target(&detail_motion,1,animate);
    }else{
        if(from){if(animate&&!s->reduced_motion)ui_transition_cache_begin(from);detail_from=from;detail_to=NULL;detail_direction=-1;detail_motion.value=0;target(&detail_motion,1,animate);}
        if(i==1||i==8){if(from&&i==1)i=return_drawer<0?8:1;target(&drawer_motion,i==8?-1:1,animate);}
        else {display_index=i==7;target(&display_motion,display_index,animate);target(&drawer_motion,0,animate);}
    }
    current=i;layout();
    ui_transition_cache_request(to);
    if(i==0||i==7)for(int j=0;j<2;j++)ui_transition_cache_request(drawer_content[j]);
#ifdef ESP_PLATFORM
    display_perf_transition_call_done();
#endif
}
void badge_ui_external_panel(lv_obj_t *p,bool open){extern void badge_panel_slide(lv_obj_t*,bool,bool);badge_panel_slide(p,open,s->reduced_motion);lv_obj_move_foreground(dimmer);lv_obj_move_foreground(sleep_cover);}
void badge_ui_refresh(void){
    if(!root)return;
    /* A remote Reduce Motion change also settles an already-running cached turn. */
    if(s->reduced_motion){
        target(&detail_motion,detail_motion.target,false);target(&drawer_motion,drawer_motion.target,false);target(&display_motion,display_motion.target,false);layout();
        extern void badge_panels_settle(lv_obj_t*);badge_panels_settle(root);
    }
    extern bool badge_ble_enabled(void);
    static uint32_t cache_state=UINT32_MAX;
    uint32_t state=(uint32_t)s->brightness|((uint32_t)s->reduced_motion<<8)|((uint32_t)s->wifi_enabled<<9)|((uint32_t)badge_ble_enabled()<<10)|((uint32_t)connected<<11)|((uint32_t)(s->wifi_connected+1)<<12)|((uint32_t)(s->ble_connected+1)<<14);
    if(state!=cache_state){
        uint32_t changed=state^cache_state;
        if(changed&0x1ff)ui_transition_cache_invalidate_reason(sliders[1],"DISPLAY_SETTING");
        if(changed&0xfff)ui_transition_cache_invalidate_reason(drawer_content[0],"CONTROL_SETTING");
        cache_state=state;
    }
    static int cache_battery=-2;if(cache_battery!=s->battery_mv){cache_battery=s->battery_mv;ui_transition_cache_invalidate(drawer_content[0]);}
    if(s->battery_mv>=0)lv_label_set_text_fmt(voltage,"%d.%02d V",s->battery_mv/1000,(s->battery_mv%1000)/10);else lv_label_set_text(voltage,"-- V");
    lv_label_set_text(wifi_text,s->wifi_enabled?"Wi-Fi  开":"Wi-Fi  关");
    extern bool badge_ble_enabled(void);bool ble_on=badge_ble_enabled();
    lv_label_set_text(ble_text,ble_on?"蓝牙  开":"蓝牙  关");
    lv_obj_set_style_bg_color(lv_obj_get_parent(wifi_text),lv_color_hex(s->wifi_enabled?UI_ACCENT:UI_SURFACE),0);
    lv_obj_set_style_text_color(wifi_text,lv_color_hex(s->wifi_enabled?0xFFFFFF:UI_TEXT),0);
    lv_obj_set_style_bg_color(lv_obj_get_parent(ble_text),lv_color_hex(ble_on?UI_ACCENT:UI_SURFACE),0);
    lv_obj_set_style_text_color(ble_text,lv_color_hex(ble_on?0xFFFFFF:UI_TEXT),0);
    if(s->live&&!connected){lv_label_set_text(wifi_text,"Wi-Fi  --");lv_label_set_text(ble_text,"蓝牙  --");}
    bool adjusting=lv_obj_has_state(sliders[0],LV_STATE_PRESSED)||lv_obj_has_state(sliders[1],LV_STATE_PRESSED);
    if(!adjusting)for(int i=0;i<2;i++){lv_label_set_text_fmt(brightness_text[i],"%d%%",s->brightness);if(!lv_obj_has_state(sliders[i],LV_STATE_PRESSED))lv_slider_set_value(sliders[i],s->brightness,LV_ANIM_OFF);}
    badge_liquid_set(reduced,s->reduced_motion,s->reduced_motion);
    if(!adjusting)lv_obj_set_style_bg_opa(dimmer,155-s->brightness*155/100,0);
    badge_wifi_refresh();if(lv_image_get_src(photo)!=wallpaper(s->wallpaper_index)){ui_transition_cache_invalidate(home[0]);lv_image_set_src(photo,wallpaper(s->wallpaper_index));}shown(photo,!(wall_pending&&s->wallpaper_index>=2));preview_label();
}
void badge_ui_control_bind(void (*cb)(int,int)){control=cb;}
void badge_ui_request(int op,int value){
    if(control){control(op,value);return;}
    if(s->live&&!connected){applying=false;preview_label();badge_ui_notice("请先连接设备");return;}
    if(op==BADGE_APPLY){badge_ui_select_wallpaper(value);badge_ui_apply_result(true);}else if(op==BADGE_SLEEP)badge_ui_sleep(value);
    else if(op==BADGE_BRIGHTNESS)badge_ui_settings(value,s->reduced_motion);else if(op==BADGE_REDUCED)badge_ui_settings(s->brightness,value);
}
bool badge_ui_select_wallpaper(int i){if(i<0||(i>=2&&(item_id(item_ordinal(i))!=i||uploaded_id!=i||!uploaded)))return false;s->wallpaper_index=i;applying=false;badge_ui_refresh();if(save_settings)save_settings();return true;}
void badge_ui_connected(bool on){connected=on;if(!on){applying=false;badge_ui_refresh();}}
void badge_ui_wall_pending(bool pending){ui_transition_cache_invalidate(home[0]);wall_pending=pending;shown(photo,!(pending&&s->wallpaper_index>=2));if(!pending)applying=false;gallery_layout();preview_label();}
void badge_ui_settings(int v,bool r){brightness_preview=-1;s->brightness=v;s->reduced_motion=r;badge_ui_refresh();if(save_settings)save_settings();}
void badge_ui_motion(void){if(sleeping)badge_ui_sleep(false);}
void badge_ui_sleep(bool asleep){if(asleep){ui_transition_cache_direct_end();direct_external=false;transition_input(false,99);}if(asleep)brightness_preview=-1;if(asleep&&current==8)badge_ui_page(display_index?7:0,false);sleeping=asleep;badge_ui_perf_enable(perf_enabled);shown(sleep_cover,asleep);if(asleep){lv_obj_move_foreground(sleep_cover);shown(toast,false);shown(status,false);}last_tick=lv_tick_get();}
bool badge_ui_is_asleep(void){return sleeping;}
bool badge_ui_gallery_clean(void){return current==0&&lv_obj_has_flag(status,LV_OBJ_FLAG_HIDDEN);}
void badge_ui_library(const int *ids,const lv_image_dsc_t *const *thumbs,int n){
    ui_transition_cache_invalidate(details[0]); /* Thumbnail pixels may change at the same address. */
    int previous=item_id(selection);library_count=n>31?31:n;
    for(int i=0;i<library_count;i++){library_ids[i]=ids[i];library_thumbs[i]=thumbs[i];}
    selection=item_ordinal(previous);delete_confirm=-1;lv_obj_stop_scroll_anim(gallery);gallery_layout();preview_label();
}
void badge_ui_loaded_photo(const lv_image_dsc_t *src,int id,bool apply_now){
    uploaded=src;uploaded_id=id;wall_pending=false;
    if(apply_now){badge_ui_select_wallpaper(id);badge_ui_apply_result(true);}
    else badge_ui_refresh();
}
void badge_ui_restore_photo(const lv_image_dsc_t *src){uploaded=src==&badge_wallpaper?NULL:src;uploaded_id=2;badge_ui_refresh();}
void badge_ui_set_photo(const lv_image_dsc_t *src){badge_ui_restore_photo(src);badge_ui_select_wallpaper(uploaded?2:0);}
void badge_ui_test_state(int *page,int *drawer,int *selected,float *turn){if(page)*page=current;if(drawer)*drawer=(int)lroundf(drawer_motion.value*1000);if(selected)*selected=selection;if(turn)*turn=lv_obj_get_scroll_x(gallery)/240.f;}

void badge_ui_apply_result(bool ok){applying=false;applied_until=ok?lv_tick_get()+1200:0;preview_label();}
/* Deterministic workloads use the same motion paths as touch. No settings writes. */
static unsigned probe_period=400;
void badge_ui_probe_period(unsigned ms){if(ms>=100&&ms<=1000)probe_period=ms;}
void badge_ui_probe_scene(int scene,unsigned elapsed){
    if(scene==0){badge_ui_page(2,false);int to=(elapsed/probe_period)%2;static int previous=-1;if(previous!=to){previous=to;commit_gallery(to);}}
    else if(scene==1)badge_ui_page((elapsed/probe_period)%2?0:8,true);
    else if(scene==2)badge_ui_page((elapsed/probe_period)%2?0:7,true);
    else if(scene==3)badge_ui_page(0,false);
    else if(scene==4)badge_ui_page((elapsed/probe_period)%2?3:4,true);
    else if(scene==5)badge_ui_probe_scene(0,elapsed);
    else if(scene==6)badge_ui_page((elapsed/probe_period)%2?3:5,true);
    else if(scene==7)badge_ui_page((elapsed/probe_period)%2?3:6,true);
    else if(scene==8)badge_ui_page((elapsed/probe_period)%2?0:1,true);
    else if(scene>=9&&scene<=11){
        static unsigned last[3]={UINT32_MAX,UINT32_MAX,UINT32_MAX};unsigned step=elapsed/probe_period;
        if(last[scene-9]!=step){last[scene-9]=step;extern void badge_ui_probe_panel(int,bool);badge_ui_probe_panel(scene-9,!(step%2));}
    }
    else if(scene==12){static unsigned last=UINT32_MAX;unsigned step=elapsed/probe_period;if(last!=step){last=step;extern void badge_apps_probe(bool);badge_apps_probe(!(step%2));}}
    else if(scene==14)badge_ui_page((elapsed/probe_period)%2?1:3,true);
    else if(scene==15)badge_ui_page((elapsed/probe_period)%2?1:4,true);
    else if(scene==13)badge_ui_page((elapsed/probe_period)%2?1:2,true);

}

void badge_ui_probe_restore(int page){
    target(&display_motion,display_motion.target,false);target(&drawer_motion,drawer_motion.target,false);
    target(&detail_motion,1,false);lv_obj_stop_scroll_anim(gallery);gallery_layout();layout();badge_ui_page(page,false);
}

void badge_ui_storage(uint32_t total_bytes,uint32_t available_bytes,unsigned free_slots,bool valid){
    if(!storage_total||!storage_free)return;
    char total[48],available[80];
    if(total_bytes)snprintf(total,sizeof(total),"Flash %lu MB",(unsigned long)(total_bytes/(1024u*1024u)));
    else snprintf(total,sizeof(total),"Flash -- MB");
    if(valid){unsigned tenths=(unsigned)((uint64_t)available_bytes*10/(1024u*1024u));snprintf(available,sizeof(available),"壁纸可用 %u.%u MB · %u 张",tenths/10,tenths%10,free_slots);}
    else snprintf(available,sizeof(available),"壁纸存储不可用");
    if(strcmp(lv_label_get_text(storage_total),total)||strcmp(lv_label_get_text(storage_free),available)){
        ui_transition_cache_invalidate_reason(storage_total,"FLASH_STORAGE");
        lv_label_set_text(storage_total,total);lv_label_set_text(storage_free,available);
    }
}
