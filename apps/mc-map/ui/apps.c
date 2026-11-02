#include "apps.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "ui_transition_cache.h"
extern void badge_panel_cancel(lv_obj_t*);
#include "grok.h"
#include "timetiles.h"
#include "pokemon.h"
#include "src/misc/cache/lv_cache.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { APP_LIST=0,APP_MAP=1,APP_GROK=5,APP_TIMETILES=8,APP_POKEMON=9 };
static lv_obj_t *panel,*parent,*map_image,*map_note,*map_position,*players[12];
static lv_image_dsc_t map_picture;
static map_view_t displayed_view;
static map_view_t view={.world=0,.zoom=1,.x=48,.z=96};
static bool (*request_map)(const map_view_t*);
static int screen,map_error,map_tiles,map_players;
static bool visible,map_loading,dragging;
static lv_point_t drag_start,drag_image_start;
static uint32_t map_at;
static float map_drag_x,map_drag_z;
static const float map_scale[]={1,2,5,10,25,50};
static const char *world_names[]={"主世界","下界","末地"};
static void show_list(void),show_map(void);
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int radius){
    lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *text,int x,int y,int w,const lv_font_t *font,uint32_t color){
    lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,int id){
    lv_obj_t *o=box(p,x,y,w,h,UI_SURFACE,14);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_SELECTED),LV_STATE_PRESSED);
    lv_obj_t *l=label(o,text,0,0,w,&font18,UI_TEXT);lv_obj_center(l);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);lv_obj_add_event_cb(o,cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)id);return o;
}
static void cancel_map(void){view.serial++;if(request_map){map_view_t cancel=view;cancel.world=-1;request_map(&cancel);}map_loading=false;}
static void clear_panel(int mode){
    ui_transition_cache_forget(panel);
    bool dynamic=mode==APP_MAP||mode==APP_GROK||mode==APP_TIMETILES||mode==APP_POKEMON;
    ui_transition_cache_pause(dynamic);
    if(dynamic)ui_memory_pressure_set(UI_MEMORY_HIGH);
    if(screen==APP_MAP)cancel_map();
    grok_close();timetiles_close();pokemon_close();
    lv_obj_clean(panel);lv_image_cache_drop(&map_picture);free((void*)map_picture.data);memset(&map_picture,0,sizeof(map_picture));
    screen=mode;dragging=false;map_image=NULL;
    lv_obj_set_style_bg_color(panel,lv_color_hex(mode==APP_MAP?0x101820:UI_BG),0);
}
static void back(lv_event_t *e){(void)e;if(screen==APP_LIST){visible=false;badge_ui_external_panel(panel,false);return;}show_list();}
static void heading(const char *text){button(panel,"‹",80,29,44,44,back,0);label(panel,text,123,34,142,&font24,UI_TEXT);}
static void show_grok(void){
    clear_panel(APP_GROK);lv_obj_set_style_bg_color(panel,lv_color_hex(0x101820),0);
    if(!grok_open(panel,show_list)){heading("Grok");label(panel,"Out of memory",80,150,200,&font18,UI_TEXT);}
}
static void show_timetiles(void){
    clear_panel(APP_TIMETILES);
    if(!timetiles_open(panel,show_list)){heading("TimeTiles");label(panel,"Out of memory",80,150,200,&font18,UI_TEXT);}
}
static void show_pokemon(void){clear_panel(APP_POKEMON);pokemon_open(panel,show_list);}
static void choose(lv_event_t *e){int id=(int)(intptr_t)lv_event_get_user_data(e);if(id==0)show_map();else if(id==1)show_grok();else if(id==2)show_timetiles();else show_pokemon();}
static void show_list(void){
    clear_panel(APP_LIST);heading("应用");
    button(panel,"MC 实时地图",68,94,224,44,choose,0);
    button(panel,"Grok",68,146,224,44,choose,1);
    button(panel,"TimeTiles",68,198,224,44,choose,2);
    button(panel,"宝可梦对战",68,250,224,44,choose,3);
    ui_transition_cache_request(panel);
}
static void request_view(void){
    view.serial++;map_loading=true;map_at=lv_tick_get();
    if(!request_map||!request_map(&view)){map_loading=false;lv_label_set_text(map_note,"暂时无法加载，点刷新重试");}
    else lv_label_set_text(map_note,"正在更新地图");
    lv_label_set_text_fmt(map_position,"X %.0f  Z %.0f",view.x,view.z);
}
static void map_event(lv_event_t *e){
    lv_event_code_t code=lv_event_get_code(e);lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_point_t p;lv_indev_get_point(in,&p);
    if(code==LV_EVENT_PRESSED){drag_start=p;drag_image_start=(lv_point_t){lv_obj_get_x(map_image),lv_obj_get_y(map_image)};map_drag_x=view.x;map_drag_z=view.z;dragging=true;}
    if(code==LV_EVENT_PRESSING&&dragging){
        int dx=p.x-drag_start.x,dy=p.y-drag_start.y;
        lv_obj_set_pos(map_image,drag_image_start.x+dx,drag_image_start.y+dy);
        for(int i=0;i<12;i++)lv_obj_add_flag(players[i],LV_OBJ_FLAG_HIDDEN);
    }
    if((code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)&&dragging){
        dragging=false;int dx=p.x-drag_start.x,dy=p.y-drag_start.y;
        if(abs(dx)+abs(dy)>=8){
            view.x=fmaxf(-29999000,fminf(29999000,map_drag_x-dx*map_scale[view.zoom]));
            view.z=fmaxf(-29999000,fminf(29999000,map_drag_z-dy*map_scale[view.zoom]));request_view();
        }else lv_obj_set_pos(map_image,drag_image_start.x,drag_image_start.y);
    }
}
static void map_control(lv_event_t *e){
    int op=(int)(intptr_t)lv_event_get_user_data(e);
    if(op==-1&&view.zoom<5)view.zoom++;
    if(op==1&&view.zoom>0)view.zoom--;
    if(op==2){view.x=view.world==0?48:0;view.z=view.world==0?96:0;}
    if(op==3){view.world=(view.world+1)%3;view.x=view.world==0?48:0;view.z=view.world==0?96:0;show_map();return;}
    request_view();
}
static void show_map(void){
    clear_panel(APP_MAP);
    map_image=lv_image_create(panel);lv_obj_remove_flag(map_image,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(map_image,0,0);
    lv_obj_t *touch=box(panel,0,0,360,360,0,0);lv_obj_set_style_bg_opa(touch,0,0);lv_obj_add_flag(touch,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(touch,map_event,LV_EVENT_ALL,NULL);
    for(int i=0;i<12;i++){players[i]=box(panel,0,0,9,9,0x3D9FFF,5);lv_obj_set_style_border_width(players[i],2,0);lv_obj_set_style_border_color(players[i],lv_color_white(),0);lv_obj_remove_flag(players[i],LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(players[i],LV_OBJ_FLAG_HIDDEN);}
    button(panel,"‹",82,28,44,44,back,0);button(panel,world_names[view.world],130,28,104,44,map_control,3);button(panel,"刷新",240,55,62,44,map_control,0);
    map_note=label(panel,"",64,80,232,&font14,0xFFFFFF);lv_obj_set_style_bg_color(map_note,lv_color_hex(0x101820),0);lv_obj_set_style_bg_opa(map_note,220,0);
    map_position=label(panel,"",63,270,234,&font14,0xFFFFFF);lv_obj_set_style_bg_color(map_position,lv_color_hex(0x101820),0);lv_obj_set_style_bg_opa(map_position,220,0);
    button(panel,"−",81,301,48,44,map_control,-1);button(panel,"出生点",133,301,94,44,map_control,2);button(panel,"+",231,301,48,44,map_control,1);
    request_view();
}
bool badge_apps_map_accepting(void){return !dragging;}
bool badge_apps_map_result(map_result_t *result){
    if(!visible||screen!=APP_MAP||result->view.serial!=view.serial){free(result->pixels);return false;}
    if(dragging){free(result->pixels);return false;}
    map_at=lv_tick_get();map_loading=result->partial;
    map_error=result->error;map_tiles=result->tiles;
    if(result->reset){
        bool clear=!map_picture.data||displayed_view.world!=result->view.world||displayed_view.zoom!=result->view.zoom||displayed_view.x!=result->view.x||displayed_view.z!=result->view.z;
        displayed_view=result->view;
        if(!map_picture.data){
#ifdef ESP_PLATFORM
            uint16_t *pixels=heap_caps_malloc(360*360*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
            uint16_t *pixels=malloc(360*360*2);
#endif
            if(pixels){
                map_picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=259200,.data=(uint8_t*)pixels};
                lv_image_set_src(map_image,&map_picture);
            }
        }
        if(clear&&map_picture.data){
            lv_image_cache_drop(&map_picture);uint16_t *pixels=(uint16_t*)map_picture.data;
            for(int i=0;i<360*360;i++)pixels[i]=0x1082;
            lv_obj_set_pos(map_image,0,0);lv_obj_invalidate(map_image);
        }
        for(int i=0;i<12;i++)lv_obj_add_flag(players[i],LV_OBJ_FLAG_HIDDEN);
    }
    if(result->pixels){
        if(map_picture.data&&result->x>=0&&result->y>=0&&result->w>0&&result->h>0&&result->x+result->w<=360&&result->y+result->h<=360){
            lv_image_cache_drop(&map_picture);uint16_t *pixels=(uint16_t*)map_picture.data;
            for(int y=0;y<result->h;y++)memcpy(pixels+(result->y+y)*360+result->x,result->pixels+y*result->w,result->w*2);
            lv_area_t area;lv_obj_get_coords(map_image,&area);
            area.x1+=result->x;area.y1+=result->y;area.x2=area.x1+result->w-1;area.y2=area.y1+result->h-1;
            lv_obj_invalidate_area(map_image,&area);
        }
        free(result->pixels);
    }
    if(!map_picture.data&&!result->error){map_error=5;lv_label_set_text(map_note,"内存不足，退出后重试");return false;}
    if(result->partial)return true;
    map_players=result->players;
    const char *error=result->error==1?"先连接可上网的 Wi-Fi":result->error==2?"校时失败，点刷新重试":result->error==5?"内存不足，退出后重试":"部分地图未加载，点刷新";
    if(result->error)lv_label_set_text(map_note,error);
    else if(result->players<0)lv_label_set_text(map_note,"地形已更新 · 玩家数据暂不可用");
    else lv_label_set_text_fmt(map_note,"在线 %d · 每 10 秒更新",result->players);
    for(int i=0;i<12;i++){
        lv_obj_add_flag(players[i],LV_OBJ_FLAG_HIDDEN);if(i>=result->count)continue;
        int x=180+(result->player[i].x-view.x)/map_scale[view.zoom],y=180+(result->player[i].z-view.z)/map_scale[view.zoom];
        if(x>=5&&x<355&&y>=5&&y<355){lv_obj_set_pos(players[i],x-4,y-4);lv_obj_remove_flag(players[i],LV_OBJ_FLAG_HIDDEN);}
    }
    return true;
}
static void tick(lv_timer_t *timer){
    (void)timer;uint32_t now=lv_tick_get();
    timetiles_poll(visible&&screen==APP_TIMETILES&&!badge_ui_is_asleep());
    pokemon_poll(visible&&screen==APP_POKEMON&&!badge_ui_is_asleep());
    if(!visible||badge_ui_is_asleep())return;
    if(screen==APP_GROK)grok_tick(now);
    if(screen==APP_MAP&&!dragging&&!map_loading&&now-map_at>=10000)request_view();
}
void badge_apps_create(lv_obj_t *p){parent=p;panel=box(parent,0,0,360,360,UI_BG,0);ui_transition_cache_register(panel,false);ui_transition_cache_identify(panel,33);show_list();lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_timer_create(tick,33,NULL);}
void badge_apps_open(void){
    if(!panel){panel=box(parent,0,0,360,360,UI_BG,0);ui_transition_cache_register(panel,false);ui_transition_cache_identify(panel,33);}
    visible=true;if(screen!=APP_LIST)show_list();badge_ui_external_panel(panel,true);
}
void badge_apps_hide(void){
    if(!visible){badge_panel_cancel(panel);return;}
    if(screen==APP_LIST){visible=false;badge_panel_cancel(panel);return;}
    visible=false;ui_transition_cache_pause(false);grok_close();timetiles_close();pokemon_close();if(screen==APP_MAP)cancel_map();
    badge_panel_cancel(panel);ui_transition_cache_forget(panel);lv_obj_clean(panel);lv_image_cache_drop(&map_picture);free((void*)map_picture.data);memset(&map_picture,0,sizeof(map_picture));
    show_list();
}
void badge_apps_bind(bool (*map)(const map_view_t*)){request_map=map;}
void badge_apps_launch(int app){badge_apps_open();if(app==7)show_pokemon();if(app==6)show_timetiles();if(app==3)show_grok();if(app==1)show_map();}
void badge_apps_motion(float x,float y,bool shake,bool available){
    grok_motion(x,y,visible&&screen==APP_GROK&&!badge_ui_is_asleep()&&shake,available);
}
void badge_apps_status(int *page,int *loading,int *error,int *tiles,int *count){*page=visible?screen:-1;*loading=map_loading;*error=map_error;*tiles=map_tiles;*count=map_players;}
void badge_apps_probe(bool open){if(open)badge_apps_open();else {visible=false;badge_ui_external_panel(panel,false);}}

int badge_apps_cache_page(void){return visible&&screen==APP_LIST?33:-1;}
