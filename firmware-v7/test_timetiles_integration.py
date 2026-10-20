"""Execute TimeTiles production persistence, command and input glue with platform stubs."""
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent
source = (root/'ui/timetiles.c').read_text(encoding='utf-8')
raster = (root/'ui/timetiles/raster.c').read_text(encoding='utf-8')
out = Path('F:/CABadgeBuild/temp/timetiles-integration-test')
out.mkdir(parents=True, exist_ok=True)


def function(text, name):
    match = re.search(r'^\w[^\n]*\b'+name+r'\([^;\n]*\)\s*\{', text, re.M)
    if not match:
        raise ValueError('Missing production function '+name)
    end = text.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


prefix = r'''
#include "core.h"
#include "raster.h"
#include "effects.h"
#include "timetiles_assets.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { int x,y,w,h,id; unsigned flags; } lv_obj_t;
typedef struct { void *data;int code; } lv_event_t;
typedef struct {int x,y;} lv_point_t;
typedef int lv_event_code_t;
typedef struct {lv_point_t p;} lv_indev_t;
static lv_indev_t indev;
static bool mock_transition,mock_sleep;
static lv_indev_t *lv_indev_active(void){return &indev;}
static void lv_indev_get_point(lv_indev_t *in,lv_point_t *p){*p=in->p;}
static lv_event_code_t lv_event_get_code(lv_event_t *e){return e->code;}
static bool badge_ui_transition_active(void){return mock_transition;}
static bool badge_ui_is_asleep(void){return mock_sleep;}
enum { LV_OBJ_FLAG_SCROLLABLE=1,LV_OBJ_FLAG_GESTURE_BUBBLE=2,LV_OBJ_FLAG_CLICKABLE=4,LV_OBJ_FLAG_HIDDEN=8,
       LV_EVENT_ALL=0,LV_EVENT_SHORT_CLICKED=1,LV_EVENT_PRESSED=2,LV_EVENT_PRESSING=3,
       LV_EVENT_RELEASED=4,LV_EVENT_PRESS_LOST=5,LV_EVENT_INDEV_RESET=6 };
static lv_obj_t observed,*controls,*image;
static lv_obj_t hit_objects[64];static unsigned hit_count;
static void *lv_event_get_user_data(lv_event_t *e){return e->data;}
static lv_obj_t *lv_obj_create(lv_obj_t *parent){(void)parent;memset(&observed,0,sizeof(observed));return &observed;}
static void lv_obj_remove_style_all(lv_obj_t *o){(void)o;}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){o->w=w;o->h=h;}
static void lv_obj_remove_flag(lv_obj_t *o,unsigned flags){o->flags&=~flags;}
static void lv_obj_add_flag(lv_obj_t *o,unsigned flags){o->flags|=flags;}
static void lv_obj_clean(lv_obj_t *o){(void)o;hit_count=0;}
static void lv_obj_invalidate(lv_obj_t *o){(void)o;}
static int picture;static void lv_image_cache_drop(void *p){(void)p;}
static void badge_ui_click_guard(lv_event_t *e){(void)e;}
static void event(lv_event_t *e);
static void lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(lv_event_t*),int kind,void *data){
 (void)cb;if(kind==LV_EVENT_SHORT_CLICKED){o->id=(int)(intptr_t)data;assert(hit_count<64);hit_objects[hit_count++]=*o;}
}
enum { ESP_OK=0,ESP_ERR_INVALID_ARG=2,ESP_ERR_NVS_NOT_FOUND=3,ESP_ERR_NVS_INVALID_LENGTH=4,
       NVS_READONLY=0,NVS_READWRITE=1 };
typedef int nvs_handle_t;
static int mock_read_error=ESP_ERR_NVS_NOT_FOUND,mock_commit_error;
static unsigned reads,writes,commits,closes;
static uint8_t blob[1024];static size_t blob_size;
static int nvs_open(const char *name,int mode,nvs_handle_t *h){
 assert(!strcmp(name,"badge_timetiles"));assert(mode==NVS_READONLY||mode==NVS_READWRITE);*h=mode;return ESP_OK;
}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size){
 assert(h==NVS_READONLY&&!strcmp(key,"state_v1"));reads++;
 if(mock_read_error)return mock_read_error;
 if(*size<blob_size){*size=blob_size;return ESP_ERR_NVS_INVALID_LENGTH;}
 memcpy(data,blob,blob_size);*size=blob_size;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
 assert(h==NVS_READWRITE&&!strcmp(key,"state_v1")&&size<=sizeof(blob));
 memcpy(blob,data,size);blob_size=size;writes++;return ESP_OK;
}
static int nvs_commit(nvs_handle_t h){assert(h==NVS_READWRITE);commits++;return mock_commit_error;}
static void nvs_close(nvs_handle_t h){(void)h;closes++;}
typedef int64_t time_t;
struct timeval { time_t tv_sec;int64_t tv_usec; };
struct tm {int tm_year,tm_mon,tm_mday,tm_hour,tm_min,tm_sec,tm_wday;};
static struct tm *gmtime_r(const time_t *value,struct tm *t){(void)value;*t=(struct tm){.tm_year=126,.tm_mon=9,.tm_mday=15,.tm_min=9,.tm_wday=4};return t;}
static uint64_t clock_set;
static uint64_t mock_mono;
static int64_t esp_timer_get_time(void){return mock_mono*1000;}
static int gettimeofday(struct timeval *tv,void *unused){(void)unused;tv->tv_sec=clock_set/1000;tv->tv_usec=clock_set%1000*1000;return 0;}
static int settimeofday(const struct timeval *tv,void *unused){(void)unused;clock_set=(uint64_t)tv->tv_sec*1000+tv->tv_usec/1000;return 0;}
static tt_core_t core;
static bool initialized,dirty,building,showing;
static tt_effect_t *effects;
static tt_canvas_t canvas;
static uint16_t *pixels;
static unsigned rotation,notified,stored_revision,renders;
static int store_error,popup,alarm_page,edit_alarm,field[3],date[3],pending;
static uint8_t edit_mask;
static uint64_t last_store,last_sync,shown_second,last_frame;
static unsigned sync_calls,control_revision;
static int last_view=-1;
static bool badge_apps_clock_sync(void){sync_calls++;return true;}
static void badge_ui_sleep(bool value){(void)value;}
static void badge_ui_page(unsigned value,bool animate){(void)value;(void)animate;}
static void draw(void);
static void (*return_to_list)(void);
static void calendar(void){date[0]=2024;date[1]=1;date[2]=1;field[0]=field[1]=field[2]=0;}
enum {SIDE=360};
'''
definitions = '\n'.join(re.findall(r'^enum \{[^}]+\};', source, re.M)) + '\n'
definitions += re.search(r'^typedef struct \{.*?\} saved_t;', source, re.M | re.S).group() + '\n'
functions = ''.join(function(source, name) for name in (
    'valid_save', 'timetiles_init', 'store', 'set_clock', 'manual_clock', 'month_days',
    'center_mask', 'small_center', 'number', 'time_text', 'hit', 'button', 'icon_button',
    'ordered_alarms', 'draw_main', 'fields', 'draw_popup', 'draw',
    'consumed', 'open_alarm', 'duration', 'event', 'timetiles_command', 'timetiles_info',
    'touch_point', 'touch', 'timetiles_motion', 'timetiles_shake', 'timetiles_poll'))
functions += function(raster, 'blend565') + function(raster, 'pixel')
tests = r'''
#define JAN1 UINT64_C(1704067200000)
static void reset_runtime(void){
 initialized=false;memset(&core,0,sizeof(core));stored_revision=0;last_store=0;
 mock_read_error=ESP_ERR_NVS_NOT_FOUND;mock_commit_error=0;reads=writes=commits=closes=0;
 notified=popup=alarm_page=edit_alarm=0;dirty=false;image=NULL;clock_set=0;hit_count=0;
}
static void load(const saved_t *s,size_t size){
 reset_runtime();mock_read_error=ESP_OK;blob_size=size;memset(blob,0,sizeof(blob));memcpy(blob,s,size<sizeof(*s)?size:sizeof(*s));
 timetiles_init();
}
static saved_t snapshot(void){saved_t s;assert(blob_size==sizeof(s));memcpy(&s,blob,sizeof(s));return s;}
static void persistence(void){
 reset_runtime();timetiles_init();assert(initialized&&store_error==ESP_OK&&reads==1&&closes==1);
 unsigned calls=reads;timetiles_init();assert(reads==calls);
 tt_tick(&core,1000,JAN1);tt_focus_duration(&core,45);tt_countdown_duration(&core,300);
 tt_toggle(&core,true);tt_toggle(&core,false);tt_tick(&core,11234,JAN1+10234);
 tt_alarm_set(&core,5,9,30,62,true);core.alarms[5].last_fired_ms=JAN1-1000;
 core.alarms[5].snooze_deadline_ms=core.monotonic_ms+300000;
 core.theme=4;core.grid=false;core.glow=false;core.revision++;
 store();saved_t s=snapshot();assert(valid_save(&s));
 assert(s.focus_remaining==2700000-10234&&s.count_remaining==300000-10234);
 assert(s.focus_status==1&&s.count_status==1&&stored_revision==core.revision&&last_store==11234);
 assert(writes==1&&commits==1&&closes==2);
 load(&s,sizeof(s));assert(core.focus.status_raw==2&&core.countdown.status_raw==2);
 assert(core.focus.remaining_ms==s.focus_remaining&&core.countdown.remaining_ms==s.count_remaining);
 assert(!core.focus.deadline_low&&!core.focus.deadline_high&&!core.countdown.deadline_low&&!core.countdown.deadline_high);
 assert(!core.monotonic_ms&&!core.alarms[5].snooze_deadline_ms);
 assert(core.alarms[5].wall_deadline_ms==s.alarms[5].deadline&&core.alarms[5].last_fired_ms==JAN1-1000);
 assert(core.theme==4&&!core.grid&&!core.glow&&core.focus_minutes==45);
 for(unsigned legacy=0;legacy<2;legacy++){s.multicolor=legacy;load(&s,sizeof(s));assert(core.theme==4&&store_error==0);}
 tt_tick(&core,900000,JAN1+900000);assert(tt_remaining(&core,true)==s.focus_remaining);
 tt_toggle(&core,true);assert(core.focus.status_raw==1&&tt_remaining(&core,true)==s.focus_remaining);
 uint32_t old=stored_revision;mock_commit_error=55;store();assert(store_error==55&&stored_revision==old);
 mock_commit_error=0;store();assert(store_error==0&&stored_revision==core.revision);
 saved_t bad;
 #define REJECT(member,value) do{bad=s;bad.member=(value);assert(!valid_save(&bad));load(&bad,sizeof(bad));assert(store_error==ESP_ERR_INVALID_ARG&&core.focus_minutes==25);}while(0)
 REJECT(magic,0);REJECT(version,2);REJECT(theme,6);REJECT(grid,2);REJECT(glow,2);REJECT(focus_rest,2);
 REJECT(focus_status,4);REJECT(count_status,4);REJECT(multicolor,2);REJECT(timezone,-721);REJECT(timezone,841);
 REJECT(focus_minutes,30);REJECT(focus_duration,1);REJECT(count_duration,0);REJECT(count_duration,86400000);
 REJECT(count_duration,1001);REJECT(focus_remaining,s.focus_duration+1);REJECT(count_remaining,s.count_duration+1);
 REJECT(alarms[5].present,2);REJECT(alarms[5].enabled,2);REJECT(alarms[5].hour,24);REJECT(alarms[5].minute,60);
 REJECT(alarms[5].mask,128);REJECT(alarms[5].deadline,-1);REJECT(alarms[5].last_fired,-1);
 #undef REJECT
 load(&s,sizeof(s)-1);assert(store_error==ESP_ERR_INVALID_ARG&&core.focus_minutes==25);
 load(&s,sizeof(s)+1);assert(store_error==ESP_ERR_NVS_INVALID_LENGTH&&core.focus_minutes==25);
 s.focus_status=s.count_status=3;s.focus_remaining=s.count_remaining=0;load(&s,sizeof(s));assert(core.focus.status_raw==3&&core.countdown.status_raw==3);
 puts("PASS own NVS namespace/version/length/value validation, remaining checkpoints, running reboot-paused, no raw mono reuse, commit errors");
}
static void rejected(const uint8_t *p,size_t size){tt_core_t before=core;assert(!timetiles_command(p,size));assert(!memcmp(&before,&core,sizeof(core)));}
static void commands(void){
 reset_runtime();uint8_t p[16]={0};assert(!timetiles_command(p,2));timetiles_init();tt_tick(&core,1000,JAN1);
 const uint8_t good[][16]={{0,3},{1,1},{2,2},{3,1,0,0,0},{4,15},{5,5,23,59,127,1},{6,0},{7,5},{8,0},{9,5}};
 const size_t lengths[]={2,2,2,5,2,6,2,2,9,2};
 for(unsigned op=0;op<10;op++){
  memcpy(p,good[op],sizeof(p));if(op==8)for(int i=0;i<8;i++)p[i+1]=(uint8_t)(JAN1>>(8*i));
  for(size_t n=0;n<13;n++)if(n!=lengths[op])rejected(p,n);
  assert(timetiles_command(p,lengths[op]));
 }
 assert(clock_set==JAN1&&core.theme==5&&!core.alarms[5].present);
 const uint8_t bad[][6]={{0,4},{1,0},{1,3},{2,0},{2,3},{3,0,0,0,0},{3,128,81,1,0},
 {3,255,255,255,255},{4,0},{4,30},{5,6,0,0,0,1},{5,0,24,0,0,1},{5,0,0,60,0,1},
 {5,0,0,0,128,1},{5,0,0,0,0,2},{6,2},{7,6},{9,6},{255,0}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)rejected(bad[i],bad[i][0]==3?5:bad[i][0]==5?6:2);
 p[0]=8;for(int i=0;i<8;i++)p[i+1]=255;rejected(p,9);
 uint64_t limits[]={JAN1-1,UINT64_C(4102444800000)};
 for(unsigned j=0;j<2;j++){for(int i=0;i<8;i++)p[i+1]=(uint8_t)(limits[j]>>(8*i));rejected(p,9);}
 p[0]=3;p[1]=0x7f;p[2]=0x51;p[3]=1;p[4]=0;assert(timetiles_command(p,5)&&core.countdown.duration_ms==86399000);
 char json[1024];timetiles_info(json,sizeof(json));assert(*json&&json[strlen(json)-1]=='}');
 size_t needed=strlen(json)+1;
 for(size_t n=1;n<needed;n++){memset(json,'X',sizeof(json));timetiles_info(json,n);assert(!*json&&json[n]=='X');}
 puts("PASS diagnostic exact lengths/all opcode boundaries/booleans/little-endian time, rejected state unchanged and JSON truncation guards");
}
static void rotations(void){
 static uint16_t bitmap[360*360];
 const int boxes[][4]={{0,0,8,11},{77,108,206,40},{244,108,39,40},{100,300,160,36}};
 for(rotation=0;rotation<4;rotation++)for(unsigned i=0;i<sizeof(boxes)/sizeof(*boxes);i++){
  int x=boxes[i][0],y=boxes[i][1],w=boxes[i][2],h=boxes[i][3];building=true;hit(x,y,w,h,123);
  assert(observed.id==123&&(observed.flags&LV_OBJ_FLAG_CLICKABLE));
  assert(observed.x>=0&&observed.y>=0&&observed.x+observed.w<=360&&observed.y+observed.h<=360);
  memset(bitmap,0,sizeof(bitmap));tt_canvas_t c={.pixels=bitmap,.rotation=rotation};
  for(int dy=0;dy<h;dy++)for(int dx=0;dx<w;dx++)pixel(&c,x+dx,y+dy,1,15);
  unsigned marked=0;int minx=360,miny=360,maxx=-1,maxy=-1;
  for(int py=0;py<360;py++)for(int px=0;px<360;px++)if(bitmap[py*360+px]){
   assert(px>=observed.x&&px<observed.x+observed.w&&py>=observed.y&&py<observed.y+observed.h);
   marked++;if(px<minx)minx=px;if(px>maxx)maxx=px;if(py<miny)miny=py;if(py>maxy)maxy=py;
  }
  assert(marked==(unsigned)(w*h)&&minx==observed.x&&miny==observed.y&&maxx+1==observed.x+observed.w&&maxy+1==observed.y+observed.h);
 }
 building=false;lv_obj_t before=observed;hit(1,2,3,4,999);assert(!memcmp(&before,&observed,sizeof(before)));
 puts("PASS production hit boxes match production raster pixel rotation for all four angles and edge rectangles");
}
static void click(int id){lv_event_t e={.data=(void*)(intptr_t)id};event(&e);}
static void confirmations(void){
 reset_runtime();timetiles_init();tt_tick(&core,1000,JAN1);tt_select(&core,TT_FOCUS,false);tt_toggle(&core,true);tt_tick(&core,2000,JAN1+1000);
 uint32_t left=tt_remaining(&core,true);click(RESET);assert(popup==VIEW_CONFIRM&&tt_remaining(&core,true)==left&&core.focus.status_raw==1);
 click(CANCEL);assert(popup==VIEW_MAIN&&core.focus.status_raw==1);
 click(RESET);click(CONFIRM);assert(!core.focus.status_raw&&tt_remaining(&core,true)==core.focus.duration_ms);
 tt_toggle(&core,true);tt_tick(&core,3000,JAN1+2000);click(DURATION);assert(popup==VIEW_CONFIRM);
 click(CONFIRM);assert(popup==VIEW_FOCUS);click(PRESET+2);assert(popup==VIEW_MAIN&&core.focus_minutes==45&&!core.focus.status_raw);
 tt_select(&core,TT_COUNTDOWN,false);tt_toggle(&core,false);tt_tick(&core,4000,JAN1+3000);
 click(PRESET);assert(popup==VIEW_CONFIRM&&core.countdown.duration_ms==600000);click(CONFIRM);assert(core.countdown.duration_ms==60000&&!core.countdown.status_raw);
 tt_select(&core,TT_ALARMS,false);tt_alarm_set(&core,2,8,0,0,true);open_alarm(2);click(DELETE);
 assert(popup==VIEW_CONFIRM&&core.alarms[2].present);click(CANCEL);assert(core.alarms[2].present);
 open_alarm(2);click(DELETE);click(CONFIRM);assert(!core.alarms[2].present);
 tt_countdown_duration(&core,1);tt_toggle(&core,false);tt_tick(&core,5000,JAN1+4000);notified=core.alert_count;
 assert(notified==1);click(BACK);assert(!core.alert_count&&!notified);
 popup=VIEW_DATE;date[0]=2024;date[1]=1;date[2]=31;click(PLUS+1);assert(date[1]==2&&date[2]==29);
 popup=VIEW_COUNT;field[0]=23;field[1]=59;field[2]=59;click(PLUS);assert(!field[0]);click(MINUS);assert(field[0]==23);
 core.alarms[0].wall_deadline_ms=JAN1+60000;popup=VIEW_TIME;calendar();field[0]=8;field[1]=2;click(SAVE);
 assert(clock_set==JAN1+120000&&!core.alarms[0].wall_deadline_ms&&popup==VIEW_MAIN);
 puts("PASS actual reset/duration/preset/delete confirmations, BACK acknowledgement, date/field wrapping and manual alarm recalibration");
}
static void touch_and_sync(void){
 reset_runtime();timetiles_init();clock_set=JAN1;sync_calls=last_sync=0;mock_mono=1000;
 timetiles_poll(false);assert(core.wall_valid&&sync_calls==1);
 mock_mono=1500;timetiles_poll(false);assert(sync_calls==1);
 mock_mono=2000;timetiles_poll(false);assert(sync_calls==2);
 tt_effect_t effect={0};effects=&effect;tt_effect_clock(effects,"12:34");
 for(unsigned r=0;r<4;r++){
  rotation=r;int x=140,y=150;lv_point_t p={x,y};
  if(r==1)p=(lv_point_t){y,359-x};else if(r==2)p=(lv_point_t){359-x,359-y};else if(r==3)p=(lv_point_t){359-y,x};
  indev.p=p;touch_point(&p);assert(p.x==x&&p.y==y);
  lv_event_t e={.code=LV_EVENT_PRESSED};touch(&e);assert(effects->held&&effects->x==x&&effects->y==y);
  e.code=LV_EVENT_PRESS_LOST;touch(&e);assert(!effects->held);
 }
 rotation=0;popup=VIEW_SETTINGS;lv_event_t e={.code=LV_EVENT_PRESSED};touch(&e);assert(!effects->held);
 popup=0;core.page=TT_FOCUS;touch(&e);assert(!effects->held);
 core.page=TT_CLOCK;mock_transition=true;touch(&e);assert(!effects->held);mock_transition=false;
 effects=NULL;
 puts("PASS actual 4-way pointer inversion, cancel/modal/page/transition guards, and valid manual wall time still requests background SNTP");
}
static bool has_hit(int id){for(unsigned i=0;i<hit_count;i++)if(hit_objects[i].id==id)return true;return false;}
static void layout(void){
 static uint16_t bitmap[360*360];lv_obj_t mock_controls={0},mock_image={0};tt_effect_t effect={0};
 reset_runtime();timetiles_init();tt_tick(&core,1000,JAN1);effects=&effect;controls=&mock_controls;image=&mock_image;pixels=bitmap;
 for(unsigned r=0;r<4;r++){
  rotation=r;last_view=-1;draw();
  assert(hit_count==3&&has_hit(BACK)&&has_hit(MORE)&&has_hit(MANUAL));
  assert(!has_hit(SETTINGS)&&!has_hit(TAB));
  for(unsigned i=0;i<hit_count;i++)for(unsigned corner=0;corner<4;corner++){
   lv_obj_t *o=&hit_objects[i];int x=o->x+((corner&1)?o->w-1:0),y=o->y+((corner&2)?o->h-1:0);
   int dx=2*x-359,dy=2*y-359;assert(dx*dx+dy*dy<=360*360);
  }
 }
 rotation=0;last_view=-1;draw();unsigned foreground=0;int xmin=360,xmax=0,ymin=360,ymax=0;
 for(int y=100;y<248;y++)for(int x=0;x<360;x++)if(bitmap[y*360+x]==canvas.color[1]||bitmap[y*360+x]==canvas.color[3]||bitmap[y*360+x]==canvas.color[4]){
  foreground++;if(x<xmin)xmin=x;if(x>xmax)xmax=x;if(y<ymin)ymin=y;if(y>ymax)ymax=y;
 }
 assert(foreground>6000&&xmax-xmin+1>=300&&ymax-ymin+1>=120);
 click(MORE);assert(popup==VIEW_MORE);draw();assert(hit_count==8&&has_hit(SETTINGS)&&has_hit(MANUAL));
 for(unsigned page=0;page<4;page++){
  assert(has_hit(TAB+page));click(TAB+page);assert(core.page==page&&popup==VIEW_MAIN);
  click(MORE);draw();
 }
 click(SETTINGS);assert(popup==VIEW_SETTINGS);click(BACK);assert(popup==VIEW_MAIN);
 click(MORE);click(MANUAL);assert(popup==VIEW_DATE);click(BACK);assert(popup==VIEW_MAIN);
 tt_select(&core,TT_CLOCK,false);effect.mode=TT_EFFECT_CODE;effect.seed=1;effect.active=true;effect.started=0;
 bool outside_old_area=false;
 for(unsigned t=500;t<2500;t+=200){core.monotonic_ms=t;draw();assert(hit_count==0&&(controls->flags&LV_OBJ_FLAG_HIDDEN));
  for(unsigned y=0;y<360;y++)if(y<60||y>=265)for(unsigned x=0;x<360;x++)if(bitmap[y*360+x]!=0x0041)outside_old_area=true;
 }
 assert(outside_old_area);tt_effect_cancel(effects);draw();assert(hit_count==3&&!(controls->flags&LV_OBJ_FLAG_HIDDEN));
 effects=NULL;image=controls=NULL;pixels=NULL;
 puts("PASS actual clean clock composition/round-glass hit areas, larger time, More modes/settings/date access and exclusive full-height effect canvas");
}
static void pointer(unsigned code,int x,int y,uint64_t at){
 lv_point_t p={x,y};if(rotation==1)p=(lv_point_t){y,359-x};else if(rotation==2)p=(lv_point_t){359-x,359-y};else if(rotation==3)p=(lv_point_t){359-y,x};
 indev.p=p;mock_mono=at;lv_event_t e={.code=(int)code};touch(&e);
}
static void gesture(int x,int y,uint64_t at){
 pointer(LV_EVENT_PRESSED,x,y,at);pointer(LV_EVENT_PRESSING,x+10,y+2,at+30);pointer(LV_EVENT_RELEASED,x+10,y+2,at+60);
}
static void gestures(void){
 static uint16_t bitmap[360*360];lv_obj_t mock_controls={0},mock_image={0};tt_effect_t effect={0};
 reset_runtime();timetiles_init();clock_set=JAN1;effects=&effect;controls=&mock_controls;image=&mock_image;pixels=bitmap;showing=true;rotation=0;last_view=-1;
 tt_tick(&core,1000,JAN1);draw();
 for(unsigned i=0;i<6;i++){
  uint64_t at=10000+i*4000;unsigned old=core.theme;gesture(180,180,at);gesture(180,180,at+200);
  mock_mono=at+260+TT_TAP_GAP-1;timetiles_poll(true);assert(core.theme==old&&effects->mode==0);
  mock_mono++;timetiles_poll(true);assert(core.theme==(old+1)%6&&effects->mode==TT_EFFECT_NOVA&&!effects->theme_advance);
  unsigned selected=core.theme;mock_mono+=2180;timetiles_poll(true);assert(core.theme==selected&&effects->mode==0);
  unsigned stores=writes;mock_mono+=800;timetiles_poll(true);assert(writes>stores||stored_revision==core.revision);assert(snapshot().theme==selected);
  for(unsigned j=0;j<effects->count;j++){tt_tile_t *tile=&effects->tiles[j];unsigned pos=(tile->home_y+2)*360+tile->home_x+2;
   assert((bitmap[pos]==tt_theme_rgb565[selected][(unsigned[]){1,3,4}[tile->color]]));
  }
 }
 assert(core.theme==0);
 gesture(180,180,40000);gesture(180,180,40200);tt_effect_clock(effects,"00:10");assert(effects->taps==2);
 int16_t xyz[3]={1000,0,0};core.orientation.candidate=90;core.orientation.since=40000;mock_mono=40680;
 timetiles_motion(xyz,true);assert(core.page==TT_CLOCK&&core.orientation.candidate==-1);timetiles_poll(true);assert(core.theme==1);
 tt_effect_clock(effects,"00:11");assert(effects->mode==TT_EFFECT_NOVA);tt_effect_cancel(effects);
 mock_mono=40800;timetiles_poll(true);assert(core.theme==1);
 gesture(180,180,44000);gesture(180,180,44200);mock_mono=44680;timetiles_poll(true);assert(core.theme==2);
 click(MORE);mock_mono=44800;timetiles_poll(true);assert(core.theme==2&&!effects->mode);click(BACK);
 for(unsigned r=0;r<4;r++)for(unsigned title=0;title<2;title++){
  rotation=r;tt_effect_cancel(effects);unsigned old=core.theme;uint64_t at=50000+r*5000+title*2000;
  int y=title?82:180;gesture(180,y,at);gesture(180,y,at+200);gesture(180,y,at+400);
  assert(effects->mode==TT_EFFECT_CODE&&!effects->theme_advance);mock_mono=at+500;timetiles_poll(true);assert(core.theme==old);
 }
 rotation=0;tt_effect_cancel(effects);unsigned old=core.theme;mock_mono=75000;timetiles_shake();assert(effects->mode==TT_EFFECT_FLY);
 timetiles_poll(true);assert(core.theme==old);tt_effect_cancel(effects);
 mock_sleep=true;timetiles_shake();assert(!effects->mode);mock_sleep=false;
 popup=VIEW_MORE;timetiles_shake();assert(!effects->mode);popup=0;
 core.page=TT_FOCUS;timetiles_shake();assert(!effects->mode);core.page=TT_CLOCK;
 mock_transition=true;timetiles_shake();assert(!effects->mode);mock_transition=false;
 showing=false;timetiles_shake();assert(!effects->mode);showing=true;core.alert_count=1;timetiles_shake();assert(!effects->mode);core.alert_count=0;
 effects=NULL;image=controls=NULL;pixels=NULL;
 puts("PASS actual six-theme double cycles/persistence/render colors, minute changes and orientation preserve tap chains, interrupted bursts retain theme, jittered canvas/title triple at four rotations, shake routing and inactive/modal/sleep/transition guards");
}
int main(void){persistence();commands();rotations();confirmations();touch_and_sync();layout();gestures();return 0;}
'''
(out/'check.c').write_text(prefix + definitions + functions + tests, encoding='utf-8')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
sources = root/'ui/timetiles'
exe = out/'check.exe'
commands = [
    [str(root/'.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-std=c11', '-O2',
     '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I'+str(sources), str(out/'check.c'),
     str(sources/'core.c'), str(sources/'readable_timer_helpers.c'), str(sources/'effects.c'), str(sources/'raster.c'), '-o', str(exe)],
    [str(exe)],
]
log = root.parent/'outputs/timetiles-gestures-20261015/integration-check.log'
log.parent.mkdir(parents=True, exist_ok=True)
with log.open('w', encoding='utf-8') as stream:
    for command in commands:
        result = subprocess.run(command, env=env, text=True, capture_output=True)
        output = result.stdout + result.stderr
        stream.write(output)
        stream.flush()
        print(output, end='')
        if result.returncode:
            raise SystemExit(result.returncode)
