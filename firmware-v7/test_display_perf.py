"""Production aggregate timing: exclude queue/wait, preserve sums and idle zero."""
from pathlib import Path
import os, subprocess, json
root=Path(__file__).resolve().parent
src=(root/'usb_screen/device/src/display_perf.c').read_text()
src='\n'.join(x for x in src.splitlines() if not x.startswith('#include'))
stage=Path('F:/CABadgeBuild/temp/display-perf-test');stage.mkdir(exist_ok=True)
pre=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CABADGE_DISPLAY_PERF 1
#define pdFALSE 0
#define eInvalid 0
typedef struct {uint64_t ulRunTimeCounter;} TaskStatus_t;
static int xTaskGetIdleTaskHandleForCore(int c){return c;}
static void vTaskGetInfo(int c,TaskStatus_t *t,int a,int b){(void)c;(void)a;(void)b;t->ulRunTimeCounter=0;}
static bool moving;
static bool badge_ui_transition_active(void){return moving;}
typedef struct {int inv_p;bool rendering_in_progress;} lv_display_t;
static lv_display_t display;
static lv_display_t *lv_display_get_default(void){return &display;}
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static unsigned heap_caps_get_free_size(int caps){return caps==4?8000000:80000;}
static unsigned heap_caps_get_minimum_free_size(int caps){return caps==4?7000000:60000;}
enum {LV_EVENT_REFR_READY,LV_EVENT_RENDER_START,LV_EVENT_RENDER_READY,LV_EVENT_FLUSH_START,LV_EVENT_FLUSH_FINISH,LV_EVENT_FLUSH_WAIT_START,LV_EVENT_FLUSH_WAIT_FINISH};
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int code;void *param;} lv_event_t;
static int lv_event_get_code(lv_event_t *e){return e->code;}
static void *lv_event_get_param(lv_event_t *e){return e->param;}
static int lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
static int lv_area_get_height(const lv_area_t *a){return a->y2-a->y1+1;}
"""
post=r"""
static void event(int code,int64_t at){clock_us=at;lv_area_t a={0,0,359,359};lv_event_t e={code,&a};display_perf_event(&e);}
int main(void){
 char out[2048];clock_us=1;assert(!display_perf_report(out,sizeof(out)));
 display_perf_transition_begin(3,4,false);moving=true;
 event(LV_EVENT_RENDER_START,100);event(LV_EVENT_FLUSH_WAIT_START,1100);
 event(LV_EVENT_FLUSH_WAIT_FINISH,3100);event(LV_EVENT_FLUSH_START,4100);
 event(LV_EVENT_FLUSH_FINISH,14100);event(LV_EVENT_RENDER_READY,15100);
 clock_us=16000;display_perf_complete(11900,true,true);
 moving=false;assert(display_perf_transition_report(out,sizeof(out),false));
 assert(strstr(out,"\"interrupted\":false")&&strstr(out,"\"pixels\":129600"));
 clock_us=1000001;assert(display_perf_report(out,sizeof(out)));puts(out);
 clock_us=2000001;assert(display_perf_report(out,sizeof(out)));puts(out);
}
"""
(stage/'test.c').write_text(pre+src+post)
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(stage/'test.c'),'-o',str(stage/'test.exe')],env=env,check=True)
a,b=map(json.loads,subprocess.check_output([str(stage/'test.exe')],text=True).splitlines())
assert a['render_us']==3000 and a['enqueue_us']==10000 and a['wait_us']==2000
assert a['complete_us']==11900 and a['bytes']==259200 and a['completed_frames']==1
assert b['rendered']==b['flush_calls']==b['completed_frames']==b['bytes']==0
print('Production display timing separation, full-frame bytes and idle window PASS')
