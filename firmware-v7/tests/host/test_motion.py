"""Run production motion/physics, Grok interaction and sensor-service logic off-board.

I2C and LVGL calls are mocked; this is not physical axis/click/display acceptance.
"""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
from pathlib import Path
import re
import subprocess

root = ROOT
out = (TEST_TEMP/'motion-test')
out.mkdir(parents=True, exist_ok=True)


def function(source, name):
    start = re.search(r'^\w[^\n]*\b' + name + r'\([^;\n]*\)\s*\{', source, re.M).start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include "motion.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t fake_ms;
static unsigned sample(badge_motion_t *m,bool sleep,uint32_t t,int x,int y,int z,int click){
 int16_t xyz[]={x,y,z};return badge_motion_sample(m,sleep,t,xyz,click);
}
static void test_engine(void){
 badge_motion_t m={0};unsigned down=0,up=0;
 for(unsigned t=0;t<=2000;t+=10){unsigned e=sample(&m,false,t,0,0,1000,0);assert(!(e&(MOTION_SHAKE|MOTION_DOUBLE_TAP|MOTION_FACE_DOWN)));up+=!!(e&MOTION_FACE_UP);}
 assert(up==1);
 for(unsigned t=2010;t<=5000;t+=10)down+=!!(sample(&m,false,t,0,0,-1000,0)&MOTION_FACE_DOWN);
 assert(down==1&&m.down);
 for(unsigned t=5010;t<=5700;t+=10)sample(&m,true,t,500,0,-860,0);
 assert(m.down); /* orientation hysteresis until clearly lifted */
 m=(badge_motion_t){0};
 for(unsigned t=0;t<3000;t+=10)assert(!(sample(&m,false,t,500,0,-860,0)&MOTION_FACE_DOWN)); /* inclined hanging badge */
 m=(badge_motion_t){0};
 for(unsigned t=0;t<3000;t+=10)assert(!(sample(&m,false,t,t%20?100:-100,0,-1000,0)&MOTION_FACE_DOWN));
 m=(badge_motion_t){0};sample(&m,true,0,0,0,1000,0);
 assert(!(sample(&m,true,10,0,0,1000,0x54)&MOTION_DOUBLE_TAP)); /* IA + single */
 assert(!(sample(&m,true,20,0,0,1000,0x24)&MOTION_DOUBLE_TAP)); /* no IA */
 assert(sample(&m,true,30,0,0,1000,0x74)&MOTION_DOUBLE_TAP); /* single + double */
 assert(!(sample(&m,false,40,0,0,1000,0x74)&MOTION_DOUBLE_TAP)); /* same pulse across wake */
 sample(&m,false,50,0,0,1000,0);
 assert(sample(&m,false,60,0,0,1000,0x64)&MOTION_DOUBLE_TAP);
 for(int axis=0;axis<3;axis++){sample(&m,false,70+axis*20,0,0,1000,0);assert(sample(&m,false,80+axis*20,0,0,1000,0x60|(1<<axis))&MOTION_DOUBLE_TAP);}
 assert(!sample(&m,false,1000,500,0,-1000,0)); /* no events across stale gap */
 assert(!sample(&m,false,2000,500,0,-1000,0x64)); /* stale click is not a new event */
 m=(badge_motion_t){0};sample(&m,false,0,0,0,1000,0);sample(&m,false,10,0,0,1000,0);
 assert(!(sample(&m,false,20,1000,0,1000,0)&MOTION_SHAKE));
 assert(sample(&m,false,90,-1000,0,1000,0)&MOTION_SHAKE);
 sample(&m,false,100,0,0,1000,0);sample(&m,false,160,1000,0,1000,0);
 assert(!(sample(&m,false,230,-1000,0,1000,0)&MOTION_SHAKE));
 m=(badge_motion_t){0};down=0;
 for(unsigned i=0;i<=1300;i+=10)down+=!!(sample(&m,false,UINT32_MAX-600+i,0,0,-1000,0)&MOTION_FACE_DOWN);
 assert(down==1); /* millis wrap */
 gravity_ball_t b={0};
 for(int i=0;i<2000;i++){gravity_ball_step(&b,.8f,-.5f,.033f);assert(hypotf(b.x,b.y)<=95.001f);}
 b=(gravity_ball_t){0};gravity_ball_step(&b,.5f,-.5f,.033f);assert(b.x>0&&b.y<0);
 gravity_ball_t c={0};gravity_ball_step(&c,.5f,-.5f,5);b=(gravity_ball_t){0};gravity_ball_step(&b,.5f,-.5f,.04f);assert(fabsf(b.x-c.x)<.0001f);
 b=(gravity_ball_t){.x=80};for(int i=0;i<11;i++)gravity_ball_step(&b,0,0,.04f);assert(b.score==1&&b.goal==1);
 b=(gravity_ball_t){.vx=40};gravity_ball_step(&b,0,0,.04f);assert(b.vx<40);
 puts("PASS motion engine: stable faces, jitter, native double-click/dedup, shake cooldown, stale data, tick wrap; ball bounds/damping/scoring");
}
typedef struct {int x,y;} lv_point_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int flags;} lv_obj_t;
typedef struct {const void *data;} lv_image_dsc_t;
typedef int lv_event_t,lv_event_code_t,lv_indev_t;
enum {LV_EVENT_PRESSED=1,LV_EVENT_PRESSING,LV_EVENT_LONG_PRESSED,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST,LV_OBJ_FLAG_HIDDEN=1};
static lv_point_t fake_point;
static uint32_t lv_tick_get(void){return fake_ms;}
static lv_event_code_t lv_event_get_code(lv_event_t *e){return *e;}
static lv_indev_t *lv_indev_active(void){static int in;return &in;}
static void lv_indev_get_point(lv_indev_t *in,lv_point_t *p){(void)in;*p=fake_point;}
static bool lv_obj_has_flag(lv_obj_t *o,int flag){return o->flags&flag;}
static void lv_obj_remove_flag(lv_obj_t *o,int flag){o->flags&=~flag;}
static void lv_obj_add_flag(lv_obj_t *o,int flag){o->flags|=flag;}
static void lv_label_set_text(lv_obj_t *o,const char *s){(void)o;(void)s;}
static int lv_color_white(void){return 0xffffff;}
static int lv_color_hex(int c){return c;}
static void lv_obj_set_style_text_color(lv_obj_t *o,int c,int s){(void)o;(void)c;(void)s;}
static void lv_obj_invalidate(lv_obj_t *o){(void)o;}
static void lv_obj_invalidate_area(lv_obj_t *o,lv_area_t *a){(void)o;(void)a;}
#define SIDE 360
#define FACE_SCALE (360.f/256.f)
typedef struct {const char *name;uint8_t pool[8],count;uint16_t transition,blink_min,blink_max;uint32_t color;} emotion_t;
#include "grok_data.h"
'''
grok = (root/'ui/grok.c').read_text(encoding='utf-8')
code += grok[grok.index('static lv_obj_t *image'):grok.index('static float clamp')]
code += 'static void background(void){full_redraw=true;}\nstatic void eye(float pts[48][2],uint16_t ink){(void)pts;(void)ink;}\n'
for name in ['clamp', 'random_between', 'rgb', 'shape', 'select_emotion', 'grok_tick', 'grok_motion', 'touch']:
    code += function(grok, name)
code += r'''
static void test_grok(void){
 static lv_obj_t obj,ctrl;static uint16_t canvas[SIDE*SIDE],backdrop[SIDE*SIDE];
 image=name_label=&obj;controls=&ctrl;pixels=canvas;base=backdrop;fake_ms=1000;last_tick=1000;select_emotion(3,fake_ms);
 grok_motion(.5f,-.5f,false,true);grok_tick(fake_ms+33);assert(want_x>0&&want_y<0&&emotion==3);
 held=true;want_x=8;want_y=4;grok_motion(-1,1,true,true);grok_tick(fake_ms+66);assert(!reacting&&want_x==8&&want_y==4);
 held=false;fake_ms=1100;grok_motion(0,0,true,true);assert(reacting&&emotion==11&&return_emotion==3);
 grok_tick(2599);assert(reacting);grok_tick(2600);assert(!reacting&&emotion==3);
 fake_ms=3000;grok_motion(0,0,true,true);lv_event_t e=LV_EVENT_PRESSED;touch(&e);assert(!reacting&&held&&emotion==3);
 e=LV_EVENT_RELEASED;touch(&e);assert(!held&&emotion==4);
 fake_ms=4000;select_emotion(0,fake_ms);active_tilt_x=active_tilt_y=0;
 for(int i=1;i<=20;i++){fake_ms+=10;grok_motion(i*.01f,0,false,true);}assert(emotion==2); /* slow tilt accumulates */
 grok_motion(1,1,false,false);grok_tick(fake_ms+33);assert(fabsf(want_x)<=7.001f&&fabsf(want_y)<=3.001f);
 image=NULL;grok_motion(0,0,true,true);assert(!reacting);
 puts("PASS production Grok: tilt, stale fallback, touch priority, transient restore/cancel, slow tilt activity, closed app");
}
typedef int esp_err_t;
typedef void *i2c_master_dev_handle_t,*i2c_master_bus_handle_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt;struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
enum {ESP_OK=0,I2C_NUM_0=0,I2C_CLK_SRC_DEFAULT=0,I2C_ADDR_BIT_LEN_7=0};
static uint8_t regs[256];static bool fail_io,ui_sleep,transition,app_available;static float app_x,app_y;
static int sleeps,wakes,timetiles_shakes;
static int64_t esp_timer_get_time(void){return (int64_t)fake_ms*1000;}
static esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c,i2c_master_bus_handle_t *b){assert(c->sda_io_num==3&&c->scl_io_num==2);*b=(void*)1;return 0;}
static esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t *c,i2c_master_dev_handle_t *d){assert(c->device_address==0x18);*d=b;return 0;}
static esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t *r,size_t n,void *p,size_t len,int timeout){if(fail_io)return -1;memcpy(p,regs+(*r&0x7f),len);return 0;}
static esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *p,size_t len,int timeout){if(fail_io)return -1;regs[p[0]]=p[1];return 0;}
static void badge_apps_motion(float x,float y,bool shake,bool available){app_available=available;app_x=x;app_y=y;}
static void fluidbox_motion(const int16_t xyz[3],bool available){if(available)assert(xyz);}
static void timetiles_motion(const int16_t xyz[3],bool available){if(available)assert(xyz);}
static void timetiles_shake(void){timetiles_shakes++;}
static bool badge_ui_is_asleep(void){return ui_sleep;}
static bool badge_ui_transition_active(void){return transition;}
static void badge_ui_sleep(bool yes){ui_sleep=yes;sleeps++;}
static void badge_ui_motion(void){ui_sleep=false;wakes++;}
'''
service = (root/'usb_screen/device/src/motion_service.c').read_text(encoding='utf-8')
code += service[service.index('static i2c_master_dev_handle_t accel;'):]
code += r'''
static void feed(int x,int y,int z,int click,unsigned dt){
 int v[]={x,y,z};regs[0x27]=8;regs[0x39]=click;
 for(int i=0;i<3;i++){uint16_t bits=(uint16_t)(v[i]*16);regs[0x28+i*2]=bits;regs[0x29+i*2]=bits>>8;}
 fake_ms+=dt;motion_service_poll();
}
static void test_service(void){
 fake_ms=10000;regs[0x0f]=0x11;motion_service_init();assert(ready&&click_ready);
 assert(regs[0x20]==0x77&&regs[0x23]==0x88&&!(regs[0x21]&8)&&regs[0x38]==0x2a);
 assert(regs[0x3a]==16&&regs[0x3b]==32&&regs[0x3c]==32&&regs[0x3d]==160);
 for(int i=0;i<50;i++)feed(0,0,1000,0,10);assert(app_available&&samples==50&&!errors);
 feed(500,-500,1000,0,10);assert(app_x>0&&app_y>0); /* configured screen mapping */
 transition=true;for(int i=0;i<140;i++)feed(0,0,-1000,0,10);assert(!ui_sleep&&pending_face_sleep);
 transition=false;feed(0,0,-1000,0,10);assert(ui_sleep&&automatic_sleep&&sleeps==1);
 for(int i=0;i<200;i++)feed(0,0,-1000,0,10);assert(ui_sleep&&sleeps==1);
 for(int i=0;i<40;i++)feed(0,0,1000,0,10);assert(!ui_sleep&&!automatic_sleep&&wakes==1&&face_wakes==1);
 ui_sleep=true;for(int i=0;i<140;i++)feed(0,0,-1000,0,10);for(int i=0;i<40;i++)feed(0,0,1000,0,10);
 assert(ui_sleep&&wakes==1); /* manual sleep does not auto-wake on orientation */
 feed(0,0,1000,0x74,10);assert(!ui_sleep&&wakes==2&&taps==1);
 feed(0,0,1000,0x74,10);assert(taps==1);feed(0,0,1000,0,10);
 ui_sleep=true;feed(0,0,-1000,0,10);feed(0,0,-1000,0x64,10);assert(!ui_sleep&&taps==2); /* explicit double-tap works face-down too */
 fail_io=true;feed(0,0,1000,0,10);assert(!app_available&&errors==1&&retrying);
 char info[512];motion_service_info(info,sizeof(info));assert(strstr(info,"\"fresh\":false"));
 unsigned before=samples;fail_io=false;feed(0,0,1000,0,990);assert(samples==before);feed(0,0,1000,0,10);assert(samples==before+1&&app_available);
 motion_service_info(info,sizeof(info));assert(strstr(info,"\"fresh\":true")&&strstr(info,"\"double_taps\":2")&&strstr(info,"\"last_click_source\":100"));
 regs[0x27]=0;fake_ms+=260;motion_service_poll();assert(!app_available);motion_service_info(info,sizeof(info));assert(strstr(info,"\"fresh\":false"));
 for(int i=0;i<200;i++)feed(0,0,1000,0,10);
 feed(1000,0,1000,0,10);for(int i=0;i<6;i++)feed(1000,0,1000,0,10);feed(0,0,1000,0,10);assert(timetiles_shakes==1);
 ui_sleep=true;for(int i=0;i<200;i++)feed(0,0,1000,0,10);
 feed(1000,0,1000,0,10);for(int i=0;i<6;i++)feed(1000,0,1000,0,10);feed(0,0,1000,0,10);assert(!ui_sleep&&timetiles_shakes==1);
 puts("PASS production sensor service: config readback, gravity preserved, mapping, deferred sleep, auto/manual wake, native click, I2C retry/staleness/status");
}
int main(void){test_engine();test_grok();test_service();}
'''
(out/'check.c').write_text(code, encoding='utf-8')
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP),
           ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe = out/'check.exe'
subprocess.run([str(ZIG), 'cc', '-O2', '-UNDEBUG',
                '-I'+str(root/'ui'), '-I'+str(root/'usb_screen'), str(out/'check.c'), '-o', str(exe)],
               env=env, check=True)
subprocess.run([str(exe)], check=True)

# Exercise the actual list construction and callback, not only the debug page.
apps = (root/'ui/apps.c').read_text(encoding='utf-8')
entry = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int lv_obj_t,lv_event_t;
enum {APP_LIST};
static lv_obj_t *panel;
static int count,launched,ids[6];static const char *titles[6];
static void clear_panel(int mode){assert(mode==APP_LIST);count=0;}
static void heading(const char *text){assert(!strcmp(text,"应用"));}
static void show_map(void){launched=1;}static void show_menu(void){launched=2;}
static void show_grok(void){launched=3;}static void show_gravity(void){launched=4;}
static void show_fluidbox(void){launched=5;}
static void show_timetiles(void){launched=6;}
static void *lv_event_get_user_data(lv_event_t *e){return (void*)(intptr_t)*e;}
static void button(lv_obj_t *p,const char *text,int x,int y,int w,int h,void(*cb)(lv_event_t*),int id){assert(count<6);ids[count]=id;titles[count++]=text;}
static void ui_transition_cache_request(lv_obj_t *p){assert(count==6);}
'''
entry += function(apps,'choose') + function(apps,'show_list')
entry += r'''
int main(void){show_list();assert(!strcmp(titles[3],"重力小球")&&!strcmp(titles[4],"FluidBox"));for(int i=0;i<6;i++){lv_event_t e=ids[i];choose(&e);assert(launched==i+1);}puts("PASS production app list: six entries and actual callbacks reach map/miner/Grok/gravity/FluidBox/TimeTiles");}
'''
(out/'entry.c').write_text(entry,encoding='utf-8')
subprocess.run([str(ZIG), 'cc', '-O2', '-UNDEBUG',
                str(out/'entry.c'), '-o', str(out/'entry.exe')],env=env,check=True)
subprocess.run([str(out/'entry.exe')],check=True)
