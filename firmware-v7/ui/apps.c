#include "apps.h"
#include "ui_transition_cache.h"
extern void badge_panel_cancel(lv_obj_t*);
#include "grok.h"
#include "src/misc/cache/lv_cache.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { APP_LIST,APP_MAP,APP_MENU,APP_GAME,APP_DEX,APP_GROK };
static lv_obj_t *panel,*parent,*map_image,*map_note,*map_position,*players[12];
static lv_obj_t *score_label,*time_label,*game_items[32],*chain,*hook,*actor,*overlay,*overlay_title,*overlay_button;
static lv_point_precise_t rope[2];
static lv_image_dsc_t map_picture;
static map_view_t view={.world=0,.zoom=1,.x=48,.z=96};
static bool (*request_map)(const map_view_t*);
static void (*save_game)(uint64_t,int);
static miner_t game;
static int screen,game_mode=-1,dex_page;
static int game_score=-1,game_seconds=-1,game_target=-1,game_level=-1,map_error,map_tiles,map_players;
static bool visible,map_loading,dragging;
static lv_point_t drag_start,drag_image_start;
static uint32_t previous_tick,map_at;
static float map_drag_x,map_drag_z;
static const float map_scale[]={1,2,5,10,25,50};
static const char *world_names[]={"主世界","下界","末地"};
static void show_list(void),show_menu(void),show_game(void),show_map(void),show_dex(void);
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
static void save(void){if(game.save_pending&&save_game){save_game(game.dex,game.best);game.save_pending=false;}}
static void cancel_map(void){view.serial++;if(request_map){map_view_t cancel=view;cancel.world=-1;request_map(&cancel);}map_loading=false;}
static void clear_panel(int mode){
    ui_transition_cache_forget(panel);
    bool dynamic=mode==APP_MAP||mode==APP_GAME||mode==APP_GROK;
    ui_transition_cache_pause(dynamic);
    if(dynamic)ui_memory_pressure_set(UI_MEMORY_HIGH);
    if(screen==APP_MAP)cancel_map();
    save();grok_close();
    lv_obj_clean(panel);lv_image_cache_drop(&map_picture);free((void*)map_picture.data);memset(&map_picture,0,sizeof(map_picture));
    screen=mode;dragging=false;map_image=NULL;game_mode=-1;
    lv_obj_set_style_bg_color(panel,lv_color_hex(mode==APP_MAP?0x101820:mode==APP_GAME?0xEBD5A4:UI_BG),0);
}
static void back(lv_event_t *e){(void)e;if(screen==APP_LIST){visible=false;badge_ui_external_panel(panel,false);return;}if(screen==APP_DEX||screen==APP_GAME)show_menu();else show_list();}
static void heading(const char *text){button(panel,"‹",80,29,44,44,back,0);label(panel,text,123,34,142,&font24,UI_TEXT);}
static void show_grok(void){
    clear_panel(APP_GROK);lv_obj_set_style_bg_color(panel,lv_color_hex(0x101820),0);
    if(!grok_open(panel,show_list)){heading("Grok");label(panel,"Out of memory",80,150,200,&font18,UI_TEXT);}
}
static void choose(lv_event_t *e){int id=(int)(intptr_t)lv_event_get_user_data(e);if(id==0)show_map();else if(id==1)show_menu();else show_grok();}
static void show_list(void){
    clear_panel(APP_LIST);heading("应用");
    button(panel,"MC 实时地图",60,100,240,54,choose,0);
    button(panel,"奶蛙矿工",60,168,240,54,choose,1);
    button(panel,"Grok",60,236,240,54,choose,2);
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
void badge_apps_map_result(map_result_t *result){
    if(!visible||screen!=APP_MAP||result->view.serial!=view.serial){free(result->pixels);return;}
    map_loading=false;map_at=lv_tick_get();
    if(dragging){free(result->pixels);return;}
    map_error=result->error;map_tiles=result->tiles;map_players=result->players;
    if(result->pixels){
        const uint8_t *old=map_picture.data;lv_image_cache_drop(&map_picture);
        map_picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=259200,.data=(uint8_t*)result->pixels};
        lv_image_set_src(map_image,&map_picture);lv_obj_set_pos(map_image,0,0);free((void*)old);
    }
    const char *error=result->error==1?"先连接可上网的 Wi-Fi":result->error==2?"校时失败，点刷新重试":result->error==5?"内存不足，退出后重试":"部分地图未加载，点刷新";
    if(result->error)lv_label_set_text(map_note,error);
    else if(result->players<0)lv_label_set_text(map_note,"地形已更新 · 玩家数据暂不可用");
    else lv_label_set_text_fmt(map_note,"在线 %d · 每 10 秒更新",result->players);
    for(int i=0;i<12;i++){
        lv_obj_add_flag(players[i],LV_OBJ_FLAG_HIDDEN);if(i>=result->count)continue;
        int x=180+(result->player[i].x-view.x)/map_scale[view.zoom],y=180+(result->player[i].z-view.z)/map_scale[view.zoom];
        if(x>=5&&x<355&&y>=5&&y<355){lv_obj_set_pos(players[i],x-4,y-4);lv_obj_remove_flag(players[i],LV_OBJ_FLAG_HIDDEN);}
    }
}
static void start(lv_event_t *e){int difficulty=(int)(intptr_t)lv_event_get_user_data(e);miner_start(&game,1,difficulty,lv_tick_get());show_game();}
static void dex(lv_event_t *e){(void)e;dex_page=0;show_dex();}
static void show_menu(void){
    clear_panel(APP_MENU);heading("奶蛙矿工");
    lv_obj_t *im=lv_image_create(panel);lv_image_set_src(im,miner_sprites[5]);lv_obj_set_pos(im,145,78);
    char score[48];snprintf(score,sizeof(score),"最高得分 %d",game.best);label(panel,score,64,154,232,&font14,UI_MUTED);
    button(panel,"简单",56,193,78,50,start,0);button(panel,"中等",141,193,78,50,start,1);button(panel,"困难",226,193,78,50,start,2);
    button(panel,"蛙蛙图鉴",104,262,152,48,dex,0);
}
static void dex_turn(lv_event_t *e){dex_page=((dex_page+(int)(intptr_t)lv_event_get_user_data(e))+8)%8;show_dex();}
static void show_dex(void){
    clear_panel(APP_DEX);heading("蛙蛙图鉴");
    for(int j=0;j<6;j++){
        int id=dex_page*6+j;if(id>=47)break;int x=64+(j%3)*80,y=104+(j/3)*80;
        lv_obj_t *cell=box(panel,x,y,72,72,UI_SURFACE,12);
        if(game.dex&(UINT64_C(1)<<id)){lv_obj_t *im=lv_image_create(cell);lv_image_set_src(im,miner_sprites[id+6]);lv_obj_set_pos(im,24,8);}else label(cell,"?",0,8,72,&font24,UI_MUTED);
        char n[16];snprintf(n,sizeof(n),"%02d",id+1);label(cell,n,0,43,72,&font14,UI_MUTED);
    }
    button(panel,"‹",88,283,50,44,dex_turn,-1);char n[24];snprintf(n,sizeof(n),"%d / 8",dex_page+1);label(panel,n,143,295,74,&font14,UI_MUTED);button(panel,"›",222,283,50,44,dex_turn,1);
}
static void action(lv_event_t *e){(void)e;miner_action(&game);}
static void pause_game(lv_event_t *e){(void)e;miner_pause(&game);}
static void resume_game(lv_event_t *e){
    (void)e;if(game.state==MINER_PAUSED)miner_pause(&game);
    else if(game.state==MINER_CLEAR||game.state==MINER_OVER){miner_start(&game,game.level+(game.state==MINER_CLEAR),game.difficulty,lv_tick_get());show_game();}
}
static void show_game(void){
    clear_panel(APP_GAME);button(panel,"‹",80,29,44,44,back,0);label(panel,"奶蛙矿工",124,39,115,&font18,UI_TEXT);button(panel,"暂停",241,37,55,44,pause_game,0);
    game_score=game_seconds=game_target=game_level=-1;
    score_label=label(panel,"",57,79,246,&font14,UI_TEXT);time_label=label(panel,"",65,102,230,&font14,UI_MUTED);
    lv_obj_t *field=box(panel,60,128,240,168,0x805835,10);lv_obj_add_flag(field,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(field,action,LV_EVENT_SHORT_CLICKED,NULL);
    for(int i=0;i<game.count;i++){
        miner_item_t *it=&game.items[i];lv_obj_t *obj;
        if(it->type==MINER_ROCK)obj=box(panel,0,0,17,16,0x77716A,6);
        else {obj=lv_image_create(panel);int idx=it->type==MINER_BIG?1:it->type==MINER_DIAMOND?2:it->type==MINER_RARE?it->rare+6:0;lv_image_set_src(obj,miner_sprites[idx]);}
        lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);game_items[i]=obj;
    }
    chain=lv_line_create(panel);lv_obj_set_style_line_color(chain,lv_color_hex(0xDDD2B4),0);lv_obj_set_style_line_width(chain,2,0);lv_obj_remove_flag(chain,LV_OBJ_FLAG_CLICKABLE);
    hook=box(panel,0,0,9,9,0xE8D15B,3);lv_obj_remove_flag(hook,LV_OBJ_FLAG_CLICKABLE);
    actor=lv_image_create(panel);lv_image_set_src(actor,miner_sprites[3]);lv_obj_remove_flag(actor,LV_OBJ_FLAG_CLICKABLE);
    button(panel,"出钩 / 收回",115,302,130,44,action,0);
    overlay=box(panel,61,133,238,145,UI_SURFACE,18);overlay_title=label(overlay,"",12,14,214,&font18,UI_TEXT);
    overlay_button=button(overlay,"继续",62,84,114,44,resume_game,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
    previous_tick=lv_tick_get();game_mode=-1;
}
static void game_frame(float dt){
    miner_step(&game,dt);save();
    if(game_score!=game.score||game_target!=game.target||game_level!=game.level){game_score=game.score;game_target=game.target;game_level=game.level;lv_label_set_text_fmt(score_label,"第 %d 关   %d / %d",game_level,game_score,game_target);}
    int sec=(int)ceilf(game.time);if(game_seconds!=sec){game_seconds=sec;lv_label_set_text_fmt(time_label,"剩余 %d 秒",sec);}
    float tx,ty;miner_tip(&game,&tx,&ty);rope[0]=(lv_point_precise_t){60+game.pivot*.3f,116+130*.3f};rope[1]=(lv_point_precise_t){60+tx*.3f,116+ty*.3f};
    lv_line_set_points(chain,rope,2);lv_obj_set_pos(hook,(int)rope[1].x-4,(int)rope[1].y-4);lv_obj_set_pos(actor,(int)rope[0].x-14,(int)rope[0].y-26);
    for(int i=0;i<game.count;i++){
        lv_obj_t *o=game_items[i];miner_item_t *it=&game.items[i];
        if(it->collected)lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_set_pos(o,(int)(60+it->x*.3f)-lv_obj_get_width(o)/2,(int)(116+it->y*.3f)-lv_obj_get_height(o)/2);
    }
    if(game_mode!=game.state){
        game_mode=game.state;
        if(game.state==MINER_PLAY)lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(overlay);
            const char *title=game.state==MINER_PAUSED?"已暂停":game.state==MINER_CLEAR?"达成目标！":game.state==MINER_OVER?"时间到，再试一次":game.state==MINER_REVEAL?"抓到稀有蛙！":"厂长：别再空钩了！";
            lv_label_set_text(overlay_title,title);lv_label_set_text(lv_obj_get_child(overlay_button,0),game.state==MINER_CLEAR?"下一关":game.state==MINER_OVER?"重试":"继续");
            if(game.state==MINER_REVEAL||game.state==MINER_ANGRY)lv_obj_add_flag(overlay_button,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(overlay_button,LV_OBJ_FLAG_HIDDEN);
        }
    }
}
static void tick(lv_timer_t *timer){
    (void)timer;uint32_t now=lv_tick_get(),elapsed=now-previous_tick;previous_tick=now;
    if(!visible||badge_ui_is_asleep())return;
    if(screen==APP_GROK)grok_tick(now);
    if(screen==APP_GAME)game_frame(fminf(elapsed/1000.f,.25f));
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
    visible=false;ui_transition_cache_pause(false);save();grok_close();if(screen==APP_MAP)cancel_map();
    badge_panel_cancel(panel);ui_transition_cache_forget(panel);lv_obj_clean(panel);lv_image_cache_drop(&map_picture);free((void*)map_picture.data);memset(&map_picture,0,sizeof(map_picture));
    show_list();
}
void badge_apps_bind(bool (*map)(const map_view_t*),void (*persist)(uint64_t,int)){request_map=map;save_game=persist;}
void badge_apps_saved(uint64_t dex,int best){game.dex=dex;game.best=best;}
void badge_apps_launch(int app){badge_apps_open();if(app==3)show_grok();if(app==1)show_map();if(app==2){miner_start(&game,1,0,lv_tick_get());show_game();}}
void badge_apps_status(int *page,int *loading,int *error,int *tiles,int *count){*page=visible?screen:-1;*loading=map_loading;*error=map_error;*tiles=map_tiles;*count=map_players;}

void badge_apps_probe(bool open){if(open)badge_apps_open();else {visible=false;badge_ui_external_panel(panel,false);}}

int badge_apps_cache_page(void){return visible&&screen==APP_LIST?33:-1;}
