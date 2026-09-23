"""Exercise production adapter draw callback with delayed/failed DMA completion."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/main.c').read_text(encoding='utf-8')
start=source.index('static void flush_poll(void){')
code=source[start:source.index('static void physical_pointer_read(',start)]
stage=Path('F:/CABadgeBuild/temp/v7-adapter-flush-check');stage.mkdir(parents=True,exist_ok=True)
unit=stage/'check.c'
unit.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define configASSERT(x) assert(x)
#define ESP_OK 0
#define ESP_ERR_NOT_ALLOWED 7
typedef int esp_err_t;
typedef void *esp_lcd_panel_handle_t;
typedef struct {bool last;} lv_display_t;
static bool flush_pending,flush_content,flush_last,probing,probe_lcd,content=true,available;
static int64_t probe_start_us;
static uint32_t probe_flush_time,probe_wait_time,probe_frame_bytes;
static struct {unsigned submitted;} data,*extra=&data;
static int submitted,measured,error,blocked;
static void display_perf_complete(uint32_t us,bool last,bool ok){(void)us;(void)last;(void)ok;}
static bool runtime_content_frame(void){return content;}
static bool lv_display_flush_is_last(lv_display_t *d){return d->last;}
static int64_t esp_timer_get_time(void){static int64_t t;return t+=100;}
static int physical_display_error(void){return error;}
static bool physical_display_complete(uint32_t *us){if(!available)return false;available=false;*us=1234;return true;}
static void physical_display_wait(void){available=true;}
static esp_err_t physical_display_draw(int a,int b,int c,int d,const void *p){(void)a;(void)b;(void)c;(void)d;assert(p);submitted++;return error;}
static void runtime_flush(uint32_t us,uint32_t wait,bool last,bool ok,bool dirty){(void)us;blocked+=wait;if(last&&ok&&dirty)measured++;}
'''+code+r'''
int main(void){
 lv_display_t d={.last=true};uint8_t pixels[8]={0};
 assert(adapter_draw(&d,NULL,3,4,5,6,pixels,NULL)==0);
 assert(flush_pending&&submitted==1&&!measured);
 content=false;flush_poll();assert(flush_pending&&!measured);
 available=true;flush_poll();assert(!flush_pending&&measured==1);
 flush_poll();assert(measured==1);
 content=true;error=9;assert(adapter_draw(&d,NULL,3,4,5,6,pixels,NULL)==9);assert(!flush_pending&&measured==1);
 error=0;probing=probe_lcd=true;probe_start_us=1;
 assert(adapter_draw(&d,NULL,3,4,5,6,pixels,NULL)==0);flush_wait(&d);
 assert(probe_frame_bytes==8&&probe_flush_time==1234&&extra->submitted==1&&blocked==100);
 probe_lcd=false;int old=submitted;assert(adapter_draw(&d,NULL,3,4,5,6,pixels,NULL)==ESP_ERR_NOT_ALLOWED);assert(old==submitted&&!flush_pending);
 puts("Adapter accounting: delayed completion, stale frame attribution, error and render-only probe PASS");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=stage/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
