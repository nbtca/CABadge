"""Check the prototype's real flush: completion ordering and frozen RGB565 pixels."""
from pathlib import Path
import os, subprocess
root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/eaf_emote.c').read_text()
a=source.index('static void flush('); b=source.index('\nstatic bool freeze_frame',a)
flush=source[a:b]
a=source.index('static struct {'); b=source.index('} stats;',a)+len('} stats;')
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int gfx_disp_t;
typedef int esp_err_t;
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static bool capturing,playing;
static int internal_before,psram_before,phase;
static struct {const uint8_t *data;} picture;
typedef struct {uint64_t render_time_us;} gfx_disp_perf_stats_t;
static bool gfx_disp_is_flushing_last(gfx_disp_t *d){return true;}
static void gfx_disp_get_perf_stats(gfx_disp_t *d,gfx_disp_perf_stats_t *s){s->render_time_us=123;}
static int64_t esp_timer_get_time(void){static int64_t t;return ++t;}
static int heap_caps_get_free_size(int caps){return 100;}
static int physical_display_transition_draw(int x1,int y1,int x2,int y2,const void *p){assert(phase==0);phase=1;return 0;}
static int physical_display_transition_drain(void){assert(phase==1);phase=2;return 0;}
static void gfx_disp_flush_ready(gfx_disp_t *d,bool swap){assert(!swap);assert(capturing?phase==0:phase==2);phase=3;}
'''+source[a:b]+flush+r'''
int main(void){
 uint16_t input[]={0x1234,0xabcd,0x5678,0xef01},output[360*360];
 memset(output,0xcc,sizeof(output));picture.data=(const uint8_t*)output;
 capturing=true;flush(NULL,4,8,6,10,input);assert(phase==3);
 assert(output[8*360+4]==0x3412&&output[8*360+5]==0xcdab);
 assert(output[9*360+4]==0x7856&&output[9*360+5]==0x01ef&&output[9*360+6]==0xcccc);
 capturing=false;playing=true;phase=0;flush(NULL,0,0,360,16,input);
 assert(phase==3&&stats.completed==1&&stats.calls==1&&stats.render==123);
 puts("PASS: DMA drained before ready; snapshot stride/byte order; frame statistics");
}
'''
out=Path('F:/CABadgeBuild/temp/emote-test');out.mkdir(parents=True,exist_ok=True)
(out/'check.c').write_text(code)
env=dict(os.environ,TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(out/'check.c'),'-o',str(out/'check.exe')],env=env,check=True)
subprocess.run([str(out/'check.exe')],check=True)
