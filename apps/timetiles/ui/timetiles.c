#include "timetiles.h"
#include "timetiles/core.h"
#include "timetiles/raster.h"
#include "timetiles/effects.h"
#include "apps.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

enum { VIEW_MAIN,VIEW_SETTINGS,VIEW_FOCUS,VIEW_COUNT,VIEW_ALARM,VIEW_CONFIRM,VIEW_DATE,VIEW_TIME,VIEW_MORE };
enum { BACK=1,SETTINGS,TAB=10,TOGGLE=20,RESET,DURATION,PREV=30,NEXT,ADD,
       EDIT=40,ENABLE=50,PRESET=60,MINUS=70,PLUS=80,SAVE=100,CANCEL,DELETE,
       WEEK=110,THEME=120,GRID,GLOW,ZONE_MINUS,ZONE_PLUS,MANUAL,DATE_NEXT,
       ACK=130,CONTINUE,CONFIRM,MORE };
static tt_core_t core;
static bool initialized,showing,dirty;
static lv_obj_t *image,*controls;
static lv_image_dsc_t picture;
static uint16_t *pixels;
static tt_effect_t *effects;
static void (*return_to_list)(void);
static tt_canvas_t canvas;
static int popup,alarm_page,edit_alarm,field[3],date[3],pending;
static uint8_t edit_mask;
static unsigned rotation,renders,notified,stored_revision;
static int store_error;
static uint64_t last_store,last_sync,shown_second,last_frame;
static int last_view=-1;
static unsigned control_revision;

typedef struct {
    uint32_t magic,version;
    uint8_t focus_minutes,theme,grid,glow,focus_rest,focus_status,count_status,multicolor;
    int16_t timezone;
    uint32_t cycles,today,date_key,focus_duration,count_duration,focus_remaining,count_remaining;
    struct { uint8_t present,enabled,hour,minute,mask,pad[3];int64_t deadline,last_fired; } alarms[6];
} saved_t;
static bool valid_save(const saved_t *s){
    if(s->magic!=0x5454584a||s->version!=1||s->theme>=6||s->grid>1||s->glow>1||s->focus_rest>1||
       s->focus_status>3||s->count_status>3||s->multicolor>1||s->timezone< -720||s->timezone>840)return false;
    if(s->focus_minutes!=15&&s->focus_minutes!=25&&s->focus_minutes!=45&&s->focus_minutes!=60)return false;
    if(s->focus_duration!=(s->focus_rest?(s->cycles&&!(s->cycles&3)?900000u:300000u):s->focus_minutes*60000u)||
       !s->count_duration||s->count_duration>86399000||s->count_duration%1000||
       s->focus_remaining>s->focus_duration||s->count_remaining>s->count_duration)return false;
    for(int i=0;i<6;i++)if(s->alarms[i].present>1||s->alarms[i].enabled>1||s->alarms[i].hour>23||
        s->alarms[i].minute>59||s->alarms[i].mask>127||s->alarms[i].deadline<0||s->alarms[i].last_fired<0)return false;
    return true;
}
void timetiles_init(void){
    if(initialized)return;
    initialized=true;tt_init(&core);
    nvs_handle_t h;saved_t s;size_t size=sizeof(s);
    store_error=nvs_open("badge_timetiles",NVS_READONLY,&h);
    if(store_error==ESP_OK){
        store_error=nvs_get_blob(h,"state_v1",&s,&size);nvs_close(h);
        if(store_error==ESP_OK&&size==sizeof(s)&&valid_save(&s)){
            core.focus_minutes=s.focus_minutes;core.theme=s.theme;core.grid=s.grid;core.glow=s.glow;
            core.focus_rest=s.focus_rest;core.timezone_minutes=s.timezone;
            core.cycles=s.cycles;core.today=s.today;core.date_key=s.date_key;
            core.focus.duration_ms=s.focus_duration;core.focus.remaining_ms=s.focus_remaining;
            core.countdown.duration_ms=s.count_duration;core.countdown.remaining_ms=s.count_remaining;
            core.focus.status_raw=s.focus_status==1?2:s.focus_status;
            core.countdown.status_raw=s.count_status==1?2:s.count_status;
            for(int i=0;i<6;i++){
                core.alarms[i]=(tt_alarm_t){.present=s.alarms[i].present,.enabled=s.alarms[i].enabled,
                    .hour=s.alarms[i].hour,.minute=s.alarms[i].minute,.weekday_mask=s.alarms[i].mask,
                    .wall_deadline_ms=s.alarms[i].deadline,.last_fired_ms=s.alarms[i].last_fired};
            }
        }else if(store_error==ESP_OK)store_error=ESP_ERR_INVALID_ARG;
    }
    if(store_error==ESP_ERR_NVS_NOT_FOUND)store_error=ESP_OK;
    stored_revision=core.revision;
}
static void store(void){
    saved_t s={.magic=0x5454584a,.version=1,.focus_minutes=core.focus_minutes,.theme=core.theme,
        .grid=core.grid,.glow=core.glow,.focus_rest=core.focus_rest,.focus_status=core.focus.status_raw,
        .count_status=core.countdown.status_raw,.timezone=core.timezone_minutes,
        .multicolor=1, /* Keep the legacy byte layout; colors now follow the saved theme. */
        .cycles=core.cycles,.today=core.today,.date_key=core.date_key,.focus_duration=core.focus.duration_ms,
        .count_duration=core.countdown.duration_ms,.focus_remaining=tt_remaining(&core,true),
        .count_remaining=tt_remaining(&core,false)};
    for(int i=0;i<6;i++){
        s.alarms[i].present=core.alarms[i].present;s.alarms[i].enabled=core.alarms[i].enabled;
        s.alarms[i].hour=core.alarms[i].hour;s.alarms[i].minute=core.alarms[i].minute;
        s.alarms[i].mask=core.alarms[i].weekday_mask;s.alarms[i].deadline=core.alarms[i].wall_deadline_ms;
        s.alarms[i].last_fired=core.alarms[i].last_fired_ms;
    }
    nvs_handle_t h;store_error=nvs_open("badge_timetiles",NVS_READWRITE,&h);
    if(store_error==ESP_OK){store_error=nvs_set_blob(h,"state_v1",&s,sizeof(s));if(store_error==ESP_OK)store_error=nvs_commit(h);nvs_close(h);}
    if(store_error==ESP_OK)stored_revision=core.revision;
    last_store=core.monotonic_ms;
}
static void center_mask(unsigned id,int y,unsigned percent,uint16_t color){tt_mask(&canvas,id,(360-tt_mask_width(id,percent))/2,y,percent,color);}
static void small_center(const char *s,int y,unsigned scale,uint16_t color){tt_small(&canvas,s,(360-(int)strlen(s)*6*scale)/2,y,scale,color);}
static void number(const char *s,int y,unsigned scale,uint16_t color){
    int width=0;for(const char *p=s;*p;p++)width+=(*p==':'?2:7)*scale;
    tt_digits(&canvas,s,(360-width)/2,y,scale,color);
}
static void time_text(char text[16],uint32_t ms,bool hours){
    unsigned seconds=(ms+999u)/1000u;
    if(hours)snprintf(text,16,"%02u:%02u:%02u",seconds/3600,seconds/60%60,seconds%60);
    else snprintf(text,16,"%02u:%02u",seconds/60,seconds%60);
}
static void event(lv_event_t *e);
static bool building;
static void hit(int x,int y,int w,int h,int id){
    if(!building)return;
    int tx=x,ty=y;
    if(rotation==1){tx=y;ty=360-x-w;int swap=w;w=h;h=swap;}
    else if(rotation==2){tx=360-x-w;ty=360-y-h;}
    else if(rotation==3){tx=360-y-h;ty=x;int swap=w;w=h;h=swap;}
    lv_obj_t *o=lv_obj_create(controls);lv_obj_remove_style_all(o);lv_obj_set_pos(o,tx,ty);lv_obj_set_size(o,w,h);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);lv_obj_add_event_cb(o,event,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)id);
}
static void button(int x,int y,int w,int h,int id,int mask){
    tt_rect(&canvas,x,y,w,h,0x21a6);tt_rect(&canvas,x+1,y+1,w-2,h-2,0x0862);
    if(mask>=0){unsigned pct=75;while(tt_mask_width(mask,pct)>w-8&&pct>30)pct-=5;
        tt_mask(&canvas,mask,x+(w-tt_mask_width(mask,pct))/2,y+(h-24)/2,pct,canvas.color[0]);}
    hit(x,y,w,h,id);
}
static void icon_button(int x,int y,int id,unsigned icon){button(x,y,40,36,id,-1);tt_icon(&canvas,icon,x+12,y+10,1,canvas.color[0]);}
static void calendar(void){
    time_t local=(time_t)((core.wall_valid?core.wall_ms:INT64_C(1704067200000))/1000)+core.timezone_minutes*60;
    struct tm t;gmtime_r(&local,&t);
    date[0]=t.tm_year+1900;date[1]=t.tm_mon+1;date[2]=t.tm_mday;
    field[0]=t.tm_hour;field[1]=t.tm_min;field[2]=t.tm_sec;
}
static int month_days(int year,int month){
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));
}
static bool set_clock(uint64_t ms){
    if(ms<UINT64_C(1704067200000)||ms>=UINT64_C(4102444800000))return false;
    struct timeval tv={.tv_sec=(time_t)(ms/1000),.tv_usec=(ms%1000)*1000};
    return settimeofday(&tv,NULL)==0;
}
static void manual_clock(void){
    int y=date[0]-(date[1]<=2),era=y/400;unsigned yoe=y-era*400;
    unsigned month=date[1]+(date[1]>2?-3:9),doy=(153*month+2)/5+date[2]-1;
    int64_t days=(int64_t)era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
    int64_t seconds=days*86400+field[0]*3600+field[1]*60+field[2]-core.timezone_minutes*60;
    if(set_clock(seconds*1000)){for(int i=0;i<6;i++)core.alarms[i].wall_deadline_ms=0;core.revision++;popup=VIEW_MAIN;}
}
static int ordered_alarms(int ids[6]){
    int n=0;for(int i=0;i<6;i++)if(core.alarms[i].present)ids[n++]=i;
    for(int i=1;i<n;i++)for(int j=i;j>0;j--){
        tt_alarm_t *a=&core.alarms[ids[j-1]],*b=&core.alarms[ids[j]];
        if(a->hour*60+a->minute<=b->hour*60+b->minute)break;
        int swap=ids[j];ids[j]=ids[j-1];ids[j-1]=swap;
    }
    return n;
}
static void draw_main(void){
    char text[40];
    if(core.page==TT_CLOCK){
        time_t local=(time_t)(core.wall_ms/1000)+core.timezone_minutes*60;struct tm t;gmtime_r(&local,&t);
        if(core.wall_valid)snprintf(text,sizeof(text),"%02d:%02d",t.tm_hour,t.tm_min);else strcpy(text,"--:--");
        tt_effect_clock(effects,text);tt_effect_draw(effects,&canvas,core.monotonic_ms);
        if(core.wall_valid){static const char *week[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
            snprintf(text,sizeof(text),"%s %02d.%02d",week[t.tm_wday],t.tm_mon+1,t.tm_mday);small_center(text,268,2,canvas.color[5]);}
        else center_mask(54,268,65,canvas.color[5]);
        hit(112,256,136,40,MANUAL);
    }else if(core.page==TT_FOCUS){
        uint32_t left=tt_remaining(&core,true),duration=core.focus.duration_ms;
        for(int i=0;i<36;i++){float angle=(i/36.f)*6.2831853f-1.5707963f;
            tt_rect(&canvas,177+92*cosf(angle),168+92*sinf(angle),6,6,(uint64_t)left*36>=(uint64_t)duration*(i+1)?canvas.color[2]:0x2187);}
        small_center(core.focus_rest?"R E S T":"F O C U S",106,2,canvas.color[1]);
        time_text(text,left,false);number(text,142,7,canvas.color[0]);
        snprintf(text,sizeof(text),"TODAY %lu",(unsigned long)core.today);small_center(text,217,2,canvas.color[5]);
        button(72,251,64,36,RESET,13);button(148,251,64,36,TOGGLE,core.focus.status_raw==1?11:core.focus.status_raw==2?12:core.focus.status_raw==3?(core.focus_rest?37:36):10);
        button(224,251,64,36,DURATION,14);
    }else if(core.page==TT_COUNTDOWN){
        time_text(text,tt_remaining(&core,false),true);number(text,137,7,canvas.color[0]);hit(18,125,324,82,DURATION);
        static const unsigned mins[]={1,5,10,25};
        for(int i=0;i<4;i++){int x=72+i*56;button(x,212,48,28,PRESET+i,-1);snprintf(text,sizeof(text),"%u",mins[i]);tt_small(&canvas,text,x+12,219,1,canvas.color[0]);}
        button(95,252,76,36,RESET,13);button(189,252,76,36,TOGGLE,core.countdown.status_raw==1?11:core.countdown.status_raw==2?12:10);
    }else{
        int ids[6],count=ordered_alarms(ids);if(alarm_page&&count<=3)alarm_page=0;
        if(!count)center_mask(23,164,75,canvas.color[5]);
        for(int row=0;row<3&&alarm_page*3+row<count;row++){
            int i=ids[alarm_page*3+row],y=108+row*46;tt_alarm_t *a=&core.alarms[i];
            button(77,y,206,40,EDIT+i,-1);snprintf(text,sizeof(text),"%02u:%02u",a->hour,a->minute);tt_small(&canvas,text,93,y+8,2,canvas.color[0]);
            const char *rule=!a->weekday_mask?"ONCE":a->weekday_mask==127?"DAILY":a->weekday_mask==62?"MON-FRI":a->weekday_mask==65?"WEEKEND":"CUSTOM";
            tt_small(&canvas,rule,185,y+14,1,canvas.color[5]);
            tt_rect(&canvas,252,y+13,16,14,a->enabled?canvas.color[2]:0x2187);hit(244,y,39,40,ENABLE+i);
        }
        icon_button(68,251,PREV,10);button(116,251,128,36,ADD,22);icon_button(252,251,NEXT,9);
    }
}
static void fields(int *values,bool is_date){
    char text[24];
    for(int i=0;i<3;i++){int y=111+i*43;
        icon_button(86,y,MINUS+i,10);icon_button(234,y,PLUS+i,9);
        snprintf(text,sizeof(text),is_date&&i==0?"%04d":"%02d",values[i]);tt_small(&canvas,text,148,y+10,2,canvas.color[0]);
        tt_mask(&canvas,is_date?(unsigned[]){58,59,60}[i]:(unsigned[]){45,44,43}[i],202,y+9,65,canvas.color[5]);
    }
}
static void draw_popup(void){
    char text[24];
    if(core.alert_count){
        tt_alert_t *a=&core.alerts[0];center_mask((unsigned[]){70,71,72,69}[a->type],108,90,canvas.color[2]);
        if(a->type==3){tt_alarm_t *alarm=&core.alarms[a->alarm_index];snprintf(text,sizeof(text),"%02u:%02u",alarm->hour,alarm->minute);number(text,161,6,canvas.color[0]);}
        else {tt_icon(&canvas,a->type==2?3:1,156,158,3,canvas.color[1]);}
        button(79,246,90,42,ACK,73);button(185,246,96,42,CONTINUE,a->type==3?74:a->type==2?75:a->type==0?36:37);
        return;
    }
    if(popup==VIEW_MORE){
        for(int i=0;i<4;i++){int x=78+(i%2)*110,y=104+(i/2)*52;
            button(x,y,94,42,TAB+i,(unsigned[]){0,1,3,2}[i]);
            if(core.page==i)tt_rect(&canvas,x+8,y+38,78,2,canvas.color[2]);}
        button(88,216,184,36,SETTINGS,5);button(88,264,184,36,MANUAL,118);
    }else if(popup==VIEW_SETTINGS){
        button(88,112,184,36,THEME,84+core.theme);
        button(88,162,84,34,GRID,-1);tt_small(&canvas,core.grid?"GRID ON":"GRID OFF",96,174,1,canvas.color[0]);
        button(188,162,84,34,GLOW,-1);tt_small(&canvas,core.glow?"GLOW ON":"GLOW OFF",196,174,1,canvas.color[0]);
        icon_button(86,211,ZONE_MINUS,10);icon_button(234,211,ZONE_PLUS,9);
        int zone=abs(core.timezone_minutes);snprintf(text,sizeof(text),"%s%02d:%02d",core.timezone_minutes<0?"-":"",zone/60,zone%60);tt_small(&canvas,text,139,222,2,canvas.color[5]);
        button(112,262,136,34,SAVE,17);
    }else if(popup==VIEW_FOCUS){
        static const int mins[]={15,25,45,60};
        for(int i=0;i<4;i++){int x=83+(i%2)*106,y=117+(i/2)*64;button(x,y,88,48,PRESET+i,-1);snprintf(text,sizeof(text),"%d",mins[i]);tt_small(&canvas,text,x+28,y+14,3,canvas.color[0]);}
        button(118,260,124,36,CANCEL,20);
    }else if(popup==VIEW_CONFIRM){
        center_mask(67,125,70,canvas.color[2]);center_mask(68,169,70,canvas.color[5]);
        button(88,248,84,40,CANCEL,20);button(188,248,84,40,CONFIRM,21);
    }else if(popup==VIEW_ALARM){
        for(int i=0;i<2;i++){int y=107+i*44;icon_button(86,y,MINUS+i,10);icon_button(234,y,PLUS+i,9);snprintf(text,sizeof(text),"%02d",field[i]);tt_small(&canvas,text,155,y+10,3,canvas.color[0]);}
        for(int i=0;i<7;i++){int x=76+i*30;button(x,204,27,28,WEEK+i,90+i);if(edit_mask&(1u<<i))tt_rect(&canvas,x+3,230,21,2,canvas.color[2]);}
        button(85,254,82,36,DELETE,19);button(181,254,94,36,SAVE,16);
    }else{
        fields(popup==VIEW_DATE?date:field,popup==VIEW_DATE);
        button(101,255,158,36,popup==VIEW_DATE?DATE_NEXT:SAVE,popup==VIEW_COUNT?18:17);
    }
}
static void draw(void){
    bool scene=!popup&&!core.alert_count&&core.page==TT_CLOCK&&effects&&effects->mode;
    int view=core.page+popup*4+(int)rotation*40+(core.alert_count?200:0)+alarm_page*400+(scene?800:0);
    building=view!=last_view||control_revision!=core.revision;
    if(building){lv_obj_clean(controls);last_view=view;control_revision=core.revision;}
    tt_raster_begin(&canvas,pixels,core.theme,rotation,core.grid&&!scene,core.glow);
    if(scene)tt_effect_draw(effects,&canvas,core.monotonic_ms);
    else{
        small_center("TIMETILES",50,1,canvas.color[5]);
        tt_icon(&canvas,4,90,49,1,canvas.color[5]);hit(78,38,40,40,BACK);
        tt_icon(&canvas,5,254,49,1,canvas.color[5]);hit(242,38,40,40,MORE);
        if(!core.alert_count){
            if(popup==VIEW_MORE)small_center("MENU",78,2,canvas.color[0]);
            else if(popup||core.page!=TT_CLOCK)center_mask(popup==VIEW_SETTINGS?5:popup==VIEW_FOCUS?38:popup==VIEW_DATE||popup==VIEW_TIME?118:(unsigned[]){0,1,3,2}[core.page],78,75,canvas.color[1]);
        }
        if(popup||core.alert_count)draw_popup();else draw_main();
    }
    if(scene)lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_image_cache_drop(&picture);lv_obj_invalidate(image);renders++;dirty=false;building=false;
}
static bool consumed(void){
    struct recovered_timer_block *t=core.page==TT_FOCUS?&core.focus:&core.countdown;
    return (t->status_raw==1||t->status_raw==2)&&tt_remaining(&core,core.page==TT_FOCUS)<t->duration_ms;
}
static void open_alarm(int index){
    edit_alarm=index;tt_alarm_t *a=&core.alarms[index];
    field[0]=a->present?a->hour:8;field[1]=a->present?a->minute:0;edit_mask=a->present?a->weekday_mask:0;popup=VIEW_ALARM;
}
static void duration(void){
    popup=core.page==TT_FOCUS?VIEW_FOCUS:VIEW_COUNT;
    unsigned seconds=core.countdown.duration_ms/1000;field[0]=seconds/3600;field[1]=seconds/60%60;field[2]=seconds%60;
}
static void event(lv_event_t *e){
    int id=(int)(intptr_t)lv_event_get_user_data(e);dirty=true;
    tt_effect_cancel(effects);
    if(id==BACK){if(core.alert_count){tt_ack(&core,false);notified=core.alert_count;return;}if(popup){popup=VIEW_MAIN;return;}if(return_to_list)return_to_list();return;}
    if(core.alert_count){if(id==ACK||id==CONTINUE){tt_ack(&core,id==CONTINUE);notified=core.alert_count;}return;}
    if(id==MORE){popup=popup==VIEW_MORE?VIEW_MAIN:VIEW_MORE;return;}
    if(id==SETTINGS){popup=VIEW_SETTINGS;return;}
    if(id>=TAB&&id<TAB+4){tt_select(&core,id-TAB,false);popup=VIEW_MAIN;return;}
    if(id==TOGGLE){tt_toggle(&core,core.page==TT_FOCUS);return;}
    if(id==RESET||id==DURATION){
        if(consumed()){pending=id;popup=VIEW_CONFIRM;return;}
        if(id==RESET)tt_reset(&core,core.page==TT_FOCUS);else duration();return;
    }
    if(id==CONFIRM){popup=VIEW_MAIN;if(pending==DELETE)tt_alarm_delete(&core,edit_alarm);else if(pending==RESET)tt_reset(&core,core.page==TT_FOCUS);else if(pending>=PRESET&&pending<PRESET+4){
        tt_countdown_duration(&core,(unsigned[]){60,300,600,1500}[pending-PRESET]);}else duration();return;}
    if(id==CANCEL){popup=VIEW_MAIN;return;}
    if(id>=PRESET&&id<PRESET+4){
        if(popup==VIEW_FOCUS){tt_focus_duration(&core,(unsigned[]){15,25,45,60}[id-PRESET]);popup=VIEW_MAIN;}
        else if(consumed()){pending=id;popup=VIEW_CONFIRM;}
        else tt_countdown_duration(&core,(unsigned[]){60,300,600,1500}[id-PRESET]);
        return;
    }
    if(id==PREV||id==NEXT){alarm_page=!alarm_page;return;}
    if(id==ADD){for(int i=0;i<6;i++)if(!core.alarms[i].present){open_alarm(i);break;}return;}
    if(id>=EDIT&&id<EDIT+6){open_alarm(id-EDIT);return;}
    if(id>=ENABLE&&id<ENABLE+6){tt_alarm_t *a=&core.alarms[id-ENABLE];tt_alarm_set(&core,id-ENABLE,a->hour,a->minute,a->weekday_mask,!a->enabled&&!a->snooze_deadline_ms);return;}
    if(id>=WEEK&&id<WEEK+7){edit_mask^=1u<<(id-WEEK);return;}
    if(id==DELETE){pending=DELETE;popup=VIEW_CONFIRM;return;}
    if(id==THEME){core.theme=(core.theme+1)%6;core.revision++;return;}
    if(id==GRID||id==GLOW){if(id==GRID)core.grid=!core.grid;else core.glow=!core.glow;core.revision++;return;}
    if(id==ZONE_MINUS||id==ZONE_PLUS){int next=core.timezone_minutes+(id==ZONE_PLUS?30:-30);if(next>=-720&&next<=840){core.timezone_minutes=next;for(int i=0;i<6;i++)core.alarms[i].wall_deadline_ms=0;core.revision++;}return;}
    if(id==MANUAL){calendar();popup=VIEW_DATE;return;}
    if(id==DATE_NEXT){popup=VIEW_TIME;return;}
    if((id>=MINUS&&id<MINUS+3)||(id>=PLUS&&id<PLUS+3)){
        bool plus=id>=PLUS;int i=id-(plus?PLUS:MINUS),*v=popup==VIEW_DATE?date:field;
        int min=popup==VIEW_DATE?(i==0?2024:1):0;
        int max=popup==VIEW_DATE?(i==0?2099:i==1?12:month_days(date[0],date[1])):(i==0?23:59);
        v[i]+=plus?1:-1;if(v[i]<min)v[i]=max;if(v[i]>max)v[i]=min;
        if(popup==VIEW_DATE&&date[2]>month_days(date[0],date[1]))date[2]=month_days(date[0],date[1]);
        return;
    }
    if(id==SAVE){
        if(popup==VIEW_COUNT){if(!tt_countdown_duration(&core,field[0]*3600+field[1]*60+field[2]))return;}
        if(popup==VIEW_ALARM)tt_alarm_set(&core,edit_alarm,field[0],field[1],edit_mask,true);
        if(popup==VIEW_TIME){manual_clock();return;}
        popup=VIEW_MAIN;store();
    }
}
static void touch_point(lv_point_t *p){
    int x=p->x,y=p->y;
    if(rotation==1){p->x=359-y;p->y=x;}
    else if(rotation==2){p->x=359-x;p->y=359-y;}
    else if(rotation==3){p->x=y;p->y=359-x;}
}
static void touch(lv_event_t *e){
    if(!effects||popup||core.alert_count||core.page!=TT_CLOCK)return;
    lv_event_code_t code=lv_event_get_code(e);unsigned action;
    if(code==LV_EVENT_PRESSED)action=TT_TOUCH_DOWN;
    else if(code==LV_EVENT_PRESSING)action=TT_TOUCH_MOVE;
    else if(code==LV_EVENT_RELEASED)action=TT_TOUCH_UP;
    else if(code==LV_EVENT_PRESS_LOST||code==LV_EVENT_INDEV_RESET)action=TT_TOUCH_CANCEL;
    else return;
    lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_point_t p;lv_indev_get_point(in,&p);touch_point(&p);
    if(badge_ui_transition_active())action=TT_TOUCH_CANCEL;
    tt_effect_pointer(effects,action,p.x,p.y,(uint64_t)esp_timer_get_time()/1000);dirty=true;
}
bool timetiles_open(lv_obj_t *parent,void (*back)(void)){
    timetiles_init();timetiles_close();return_to_list=back;popup=VIEW_MAIN;rotation=0;alarm_page=0;last_view=-1;
    pixels=heap_caps_malloc(360*360*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!pixels)return false;
    effects=heap_caps_calloc(1,sizeof(*effects),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!effects){free(pixels);pixels=NULL;return false;}
    image=lv_image_create(parent);lv_obj_add_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(image,0,0);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(image,touch,LV_EVENT_ALL,NULL);
    picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=259200,.data=(uint8_t*)pixels};
    lv_image_set_src(image,&picture);
    controls=lv_obj_create(parent);lv_obj_remove_style_all(controls);lv_obj_set_pos(controls,0,0);lv_obj_set_size(controls,360,360);
    lv_obj_remove_flag(controls,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    core.orientation.candidate=core.orientation.accepted=-1;showing=true;dirty=true;draw();return true;
}
void timetiles_close(void){
    if(image&&initialized)store();
    if(controls){lv_obj_delete(controls);controls=NULL;}
    if(image){lv_obj_delete(image);image=NULL;}
    lv_image_cache_drop(&picture);free(pixels);pixels=NULL;free(effects);effects=NULL;picture=(lv_image_dsc_t){0};showing=false;last_view=-1;
}
void timetiles_motion(const int16_t xyz[3],bool available){
    if(!initialized)return;
    if(!showing||popup||core.alert_count||(effects&&(effects->held||effects->mode||effects->taps))||!available){core.orientation.candidate=-1;core.orientation.since=0;return;}
    if(tt_motion(&core,xyz,(uint64_t)esp_timer_get_time()/1000)){rotation=core.orientation.accepted/90;dirty=true;}
}
void timetiles_shake(void){
    if(!image||!showing||popup||core.alert_count||core.page!=TT_CLOCK||badge_ui_is_asleep()||badge_ui_transition_active())return;
    if(tt_effect_shake(effects,(uint64_t)esp_timer_get_time()/1000)){
        core.orientation.candidate=-1;core.orientation.since=0;dirty=true;
    }
}
void timetiles_poll(bool visible){
    if(!initialized)return;
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;struct timeval tv;gettimeofday(&tv,NULL);
    uint32_t revision=core.revision;tt_tick(&core,now,(int64_t)tv.tv_sec*1000+tv.tv_usec/1000);
    showing=visible;
    if(now-last_sync>=1000){last_sync=now;badge_apps_clock_sync();}
    if(core.alert_count>notified){notified=core.alert_count;badge_ui_sleep(false);badge_ui_page(15,false);showing=true;dirty=true;}
    if(!core.alert_count)notified=0;
    bool interactive=image&&showing&&!popup&&!core.alert_count&&core.page==TT_CLOCK;
    if(!interactive)tt_effect_cancel(effects);
    else if(tt_effect_tick(effects,now)){
        dirty=true;
    }
    if(effects&&effects->theme_advance){effects->theme_advance=false;core.theme=(core.theme+1)%6;core.revision++;dirty=true;}
    if(now-last_store>=750&&stored_revision!=core.revision)store();
    else if(now-last_store>=30000&&(core.focus.status_raw==1||core.countdown.status_raw==1))store();
    if(revision!=core.revision)dirty=true;
    uint64_t second=now/1000;
    if(image&&showing&&(dirty||second!=shown_second)&&now-last_frame>=33){shown_second=second;last_frame=now;draw();}
}
bool timetiles_command(const uint8_t *p,size_t n){
    if(!initialized||!n)return false;
    if(n==2&&p[0]==0&&p[1]<4){tt_select(&core,p[1],false);popup=VIEW_MAIN;}
    else if(n==2&&p[0]==1&&(p[1]==1||p[1]==2))tt_toggle(&core,p[1]==1);
    else if(n==2&&p[0]==2&&(p[1]==1||p[1]==2))tt_reset(&core,p[1]==1);
    else if(n==5&&p[0]==3){uint32_t seconds=0;for(int i=0;i<4;i++)seconds|=(uint32_t)p[i+1]<<(i*8);if(!tt_countdown_duration(&core,seconds))return false;}
    else if(n==2&&p[0]==4){if(!tt_focus_duration(&core,p[1]))return false;}
    else if(n==6&&p[0]==5&&p[5]<=1){if(!tt_alarm_set(&core,p[1],p[2],p[3],p[4],p[5]))return false;}
    else if(n==2&&p[0]==6&&p[1]<=1){tt_ack(&core,p[1]);notified=core.alert_count;}
    else if(n==2&&p[0]==7&&p[1]<6){core.theme=p[1];core.revision++;}
    else if(n==9&&p[0]==8){uint64_t ms=0;for(int i=0;i<8;i++)ms|=(uint64_t)p[i+1]<<(i*8);if(!set_clock(ms))return false;}
    else if(n==2&&p[0]==9&&p[1]<6)tt_alarm_delete(&core,p[1]);
    else return false;
    tt_effect_cancel(effects);dirty=true;return true;
}
void timetiles_info(char *out,size_t size){
    int n=snprintf(out,size,"{\"active\":%s,\"page\":%u,\"angle\":%u,\"wall_valid\":%s,\"epoch_ms\":%lld,\"timezone\":%d,\"focus_status\":%u,\"focus_ms\":%u,\"rest\":%s,\"cycles\":%u,\"today\":%u,\"countdown_status\":%u,\"countdown_ms\":%u,\"duration_s\":%u,\"theme\":%u,\"alerts\":%u,\"dropped\":%u,\"renders\":%u,\"image_bytes\":%u,\"store_error\":%d,\"alarms\":[",
        image?"true":"false",core.page,rotation*90,core.wall_valid?"true":"false",(long long)core.wall_ms,core.timezone_minutes,
        (unsigned)core.focus.status_raw,(unsigned)tt_remaining(&core,true),core.focus_rest?"true":"false",(unsigned)core.cycles,(unsigned)core.today,
        (unsigned)core.countdown.status_raw,(unsigned)tt_remaining(&core,false),(unsigned)(core.countdown.duration_ms/1000),core.theme,core.alert_count,
        (unsigned)core.alert_dropped,renders,image?259200:0,store_error);
    if(n<0||(size_t)n>=size){if(size)*out=0;return;}
    for(int i=0;i<6;i++){
        tt_alarm_t *a=&core.alarms[i];int used=snprintf(out+n,size-n,"%s[%u,%u,%u,%u,%u,%lld,%llu]",i?",":"",a->present,a->enabled,a->hour,a->minute,a->weekday_mask,(long long)a->wall_deadline_ms,(unsigned long long)a->snooze_deadline_ms);
        if(used<0||(size_t)used>=size-n){*out=0;return;}n+=used;
    }
    int tail=snprintf(out+n,size-n,"]}");
    if(tail<0||(size_t)tail>=size-n)*out=0;
}
