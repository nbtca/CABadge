"""Check actual GUI flush callbacks against delayed worker completion (no simulator)."""
from pathlib import Path
import os,subprocess

root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/main.c').read_text(encoding='utf-8')
code=source[source.index('static void flush_complete('):source.index('static void physical_pointer_read(')]
stage=Path('F:/CABadgeBuild/temp/v7-async-flush-check');stage.mkdir(parents=True,exist_ok=True)
unit=stage/'check.c'
unit.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define configASSERT(x) assert(x)
typedef struct {bool last;unsigned ready;} lv_display_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {uint32_t queue_us,wire_us,wait_us;} lcd_profile_t;
typedef struct {uint32_t flush_us,copy_us;lcd_profile_t dma;int error;bool last;} display_result_t;
static bool flush_pending,flush_content,probing,probe_lcd,content=true,available;
static lv_display_t display,*lcd_display=&display;
static int64_t probe_start_us;
static uint32_t probe_flush_time,probe_copy_time,probe_wait_time,probe_frame_bytes;
static struct {uint32_t queue_us,wire_us,wait_us,submitted;} data,*extra=&data;
static unsigned queued,completed,measured,blocked_total;static const uint8_t *owned;
static display_result_t result;
static int64_t esp_timer_get_time(void){static int64_t now;return now+=100;}
static void lv_display_flush_ready(lv_display_t *d){assert(!flush_pending);d->ready++;}
static bool lv_display_flush_is_last(lv_display_t *d){return d->last;}
static int lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
static int lv_area_get_height(const lv_area_t *a){return a->y2-a->y1+1;}
static bool runtime_content_frame(void){return content;}
static void runtime_flush(uint32_t us,uint32_t blocked,bool last,bool ok,bool dirty){
 assert(us==1234);blocked_total+=blocked;if(last&&ok&&dirty)measured++;
}
static void display_worker_submit(const lv_area_t *a,const uint8_t *p,bool last){
 assert(a->x1==3&&queued==completed);queued++;owned=p;result=(display_result_t){.flush_us=1234,.copy_us=90,.last=last};
}
static bool display_worker_poll(display_result_t *out){
 if(!available)return false;available=false;completed++;*out=result;return true;
}
static display_result_t display_worker_wait(void){display_result_t out;assert(display_worker_poll(&out));return out;}
'''+code+r'''
int main(void){
 uint8_t a[8]={0},b[8]={0};lv_area_t area={3,4,4,5};display.last=true;
 flush(&display,&area,a);assert(flush_pending&&owned==a&&display.ready==0);
 b[0]=7;flush_poll();assert(flush_pending&&display.ready==0&&queued==1);
 content=false;available=true;flush_poll();assert(!flush_pending&&display.ready==1&&measured==1);
 flush_poll();assert(display.ready==1);
 content=true;flush(&display,&area,b);result.error=7;available=true;flush_wait(&display);
 assert(display.ready==2&&measured==1&&blocked_total==100);
 probing=true;probe_lcd=true;probe_start_us=1;
 flush(&display,&area,a);available=true;flush_wait(&display);
 assert(probe_frame_bytes==8&&probe_flush_time==1234&&probe_copy_time==90&&extra->submitted==1);
 probe_lcd=false;unsigned sent=queued;flush(&display,&area,b);assert(queued==sent&&!flush_pending&&display.ready==4);
 puts("GUI async flush: no early/double ready, completion ownership, late frame accounting, error and probe PASS");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=stage/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
