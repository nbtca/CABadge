"""Host contract checks for adapter DMA completion/error draining and physical touch.
Does not claim physical LCD visibility or scan synchronization.
"""
from pathlib import Path
import os,re,subprocess
root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/physical_display.c').read_text(encoding='utf-8')
source+='\n'+(root/'usb_screen/device/src/backlight_curve.h').read_text(encoding='utf-8').replace('static inline','static')
names=('owned_refresh','backlight_duty','touch_decode','physical_touch_read','color_done','physical_display_draw','physical_display_complete','physical_display_wait')
functions='\n'.join(re.search(r'(?:static )?(?:void|bool|unsigned|esp_err_t)(?: IRAM_ATTR)? '+n+r'\(.*?\n}',source,re.S).group() for n in names)
unit=Path('F:/CABadgeBuild/temp/adapter_contract.c')
unit.write_text(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define frame_trace(code,value) ((void)0)
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 99
#define ESP_ERR_INVALID_ARG 98
#define PHYSICAL_STAGING_ROWS 16
#define ESP_ERROR_CHECK(x) assert((x)==0)
typedef int esp_err_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef int esp_lcd_panel_io_event_data_t;
static void *panel,*panel_io,*display;
static atomic_bool painted,completed,transition_owner,notify_flush;
static uint8_t block_a[11520],block_b[11520],reference[259200];
static uint8_t *staging[2]={block_a,block_b};
static const uint8_t *inflight;static int inflight_y,inflight_rows;
static bool color_done(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*);
typedef struct {bool paused;} lv_timer_t;
static int refresh_calls;
static void fake_refresh(lv_timer_t *t){(void)t;refresh_calls++;}
static void (*adapter_refresh)(lv_timer_t*)=fake_refresh;
static bool physical_display_transition_active(void){return atomic_load(&transition_owner);}
static void lv_timer_pause(lv_timer_t *t){t->paused=true;}

static _Atomic esp_err_t display_error;
static int64_t transfer_start,fake_time;
static uint32_t transfer_us;
static int submitted,notified,drained,draw_error;
static int64_t esp_timer_get_time(void){return fake_time+=10;}
static bool esp_lv_adapter_display_notify_color_trans_done_from_isr(void *d){(void)d;assert(painted);notified++;return false;}
static esp_err_t esp_lcd_panel_draw_bitmap(void *p,int x1,int y1,int x2,int y2,const void *pixels){
 (void)p;assert(x1==0&&x2==360&&y1>=0&&y2<=360&&y2-y1<=16&&pixels);
 assert(!inflight);submitted++;if(!draw_error){inflight=pixels;inflight_y=y1;inflight_rows=y2-y1;}return draw_error;
}
static esp_err_t esp_lcd_panel_io_tx_param(void *p,int cmd,const void *data,size_t n){
 (void)p;assert(cmd==-1&&!data&&!n);drained++;
 if(inflight){assert(!memcmp(inflight,reference+inflight_y*720,inflight_rows*720));inflight=NULL;color_done(NULL,NULL,NULL);}return 0;
}
typedef struct {int dummy;} lv_indev_t;
typedef struct {struct {int x,y;} point;int state;} lv_indev_data_t;
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0
static bool touch_ok=true;static int touch,touch_error,resets,waits;static uint8_t touch_raw[6];
static int i2c_master_transmit_receive(int handle,const void *reg,size_t rn,void *out,size_t n,int timeout){
 (void)handle;(void)reg;(void)rn;(void)timeout;memcpy(out,touch_raw,n);return touch_error;
}
static void lv_indev_reset(lv_indev_t *i,void *obj){(void)i;(void)obj;resets++;}
static void lv_indev_wait_release(lv_indev_t *i){(void)i;waits++;}
'''+functions+r'''
int main(void){
 uint8_t *pixels=reference;for(int i=0;i<259200;i++)pixels[i]=(uint8_t)(i*13);uint32_t us=0;
 assert(physical_display_draw(0,0,360,360,pixels)==0);
 assert(submitted==23&&!painted&&!completed&&!notified);
 assert(!physical_display_complete(&us));
 physical_display_wait();
 assert(painted&&notified==1&&physical_display_complete(&us)&&us>0);
 assert(!physical_display_complete(&us));
 lv_timer_t timer={0};owned_refresh(&timer);assert(refresh_calls==1);
 atomic_store(&transition_owner,true);
 owned_refresh(&timer);assert(timer.paused&&refresh_calls==1);
 assert(physical_display_draw(0,0,360,360,pixels)==ESP_ERR_INVALID_STATE);
 color_done(NULL,NULL,NULL);assert(notified==1&&!physical_display_complete(&us));
 atomic_store(&transition_owner,false);
 int before=drained;draw_error=7;assert(physical_display_draw(0,0,360,360,pixels)==7);
 assert(drained==before+2&&display_error==7&&notified==1);
 physical_display_wait();assert(drained==before+3);
 assert(backlight_duty(100,false)==1023&&backlight_duty(200,false)==1023&&backlight_duty(70,true)==0&&backlight_duty(-1,false)==0);
 assert(backlight_duty(10,false)==10&&backlight_duty(50,false)==256);
 for(int i=10;i<=100;i++){assert(backlight_duty(i,true)==0);if(i>10)assert(backlight_duty(i,false)>backlight_duty(i-1,false));}
 int x=12,y=13;uint8_t raw[]={0,1,1,103,1,103};assert(touch_decode(raw,&x,&y)&&x==359&&y==359);
 raw[3]=104;assert(!touch_decode(raw,&x,&y)&&x==359);raw[3]=0;raw[1]=0;assert(!touch_decode(raw,&x,&y));
 lv_indev_t indev={0};lv_indev_data_t input={0};
 memcpy(touch_raw,(uint8_t[]){0,1,0,100,0,120},6);fake_time+=20000;
 physical_touch_read(&indev,&input);assert(input.state==1&&input.point.x==100&&resets==0);
 touch_error=7;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==1&&waits==1);
 touch_error=0;touch_raw[1]=0;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==1);
 touch_raw[1]=1;touch_raw[2]=1;touch_raw[3]=104;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==2&&waits==2);

 puts("Shared staging bytes/lifetime, single final completion, ownership, error drain, touch cancellation, brightness PASS");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=Path('F:/CABadgeBuild/temp/adapter_contract.exe')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
