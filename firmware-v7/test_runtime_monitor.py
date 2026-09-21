"""Exercise production FPS accounting: idle HUD, failed flush and multi-chunk frames."""
from pathlib import Path
import os,re,subprocess
root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/runtime_monitor.c').read_text(encoding='utf-8')
code=source[source.index('static bool hud_update'):source.index('static bool append')]
unit=root/'build/runtime_monitor_check.c'
unit.write_text(r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
enum {MON_GROUPS=8,LV_EVENT_INVALIDATE_AREA,LV_EVENT_RENDER_START,LV_EVENT_RENDER_READY};
typedef int lv_event_code_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int code;lv_area_t *area;} lv_event_t;
static uint32_t now=100;static bool enabled=true,asleep;static char text[48];
static bool badge_ui_perf_enabled(void){return enabled;}
static bool badge_ui_is_asleep(void){return asleep;}
static bool badge_ui_perf_covers(const lv_area_t *a){return enabled&&a&&a->x1>=88&&a->x2<=271&&a->y1>=312&&a->y2<=329;}
static uint32_t lv_tick_get(void){return now;}
static int64_t esp_timer_get_time(void){return now*1000;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static void *lv_event_get_param(lv_event_t *e){return e->area;}
static void badge_ui_perf_text(const char *s){snprintf(text,sizeof(text),"%s",s);}
'''+code+r'''
static void event(int code,lv_area_t *area){lv_event_t e={code,area};runtime_event(&e);}
static void frame(lv_area_t area,bool ok){event(LV_EVENT_INVALIDATE_AREA,&area);event(LV_EVENT_RENDER_START,0);lv_area_t rounding={0,0,0,17};event(LV_EVENT_INVALIDATE_AREA,&rounding);now+=10;runtime_flush(2000,2000,false,true,runtime_content_frame());now+=10;runtime_flush(2000,2000,true,ok,runtime_content_frame());event(LV_EVENT_RENDER_READY,0);}
int main(void){
 runtime_poll();frame((lv_area_t){88,312,271,327},true);assert(rendered==0&&submitted==0);
 frame((lv_area_t){88,312,271,327},true);assert(rendered==0&&submitted==0&&!content_dirty);
 now=1100;runtime_poll();assert(!strcmp(text,"UI 0.0 | LCD 0.0"));
 frame((lv_area_t){0,0,359,359},true);assert(rendered==1&&submitted==1&&render_us==16000&&bus_us==4000);
 frame((lv_area_t){0,0,359,359},false);assert(rendered==2&&submitted==1);
 enabled=false;frame((lv_area_t){0,0,359,359},true);assert(rendered==2&&submitted==1);
 diagnostic_at=now;frame((lv_area_t){0,0,359,359},true);assert(rendered==3&&submitted==2);
 now+=5001;frame((lv_area_t){0,0,359,359},true);assert(rendered==3&&submitted==2);
 /* A HUD frame must not steal the previous content frame's completion. */
 diagnostic_at=now;event(LV_EVENT_INVALIDATE_AREA,&(lv_area_t){0,0,359,359});event(LV_EVENT_RENDER_START,0);
 bool pending=runtime_content_frame();now+=10;event(LV_EVENT_RENDER_READY,0);
 unsigned done=submitted;event(LV_EVENT_INVALIDATE_AREA,&(lv_area_t){88,312,271,327});event(LV_EVENT_RENDER_START,0);
 runtime_flush(5000,0,true,true,pending);assert(submitted==done+1);now+=5;event(LV_EVENT_RENDER_READY,0);
 puts("Production monitor: idle HUD excluded; split flush counts once; failed LCD excluded; disabled/timeout PASS");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=root/'build/runtime_monitor_check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
