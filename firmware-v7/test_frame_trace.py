"""Bounded transition recorder contract; not a hardware timing benchmark."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
s=(root/'usb_screen/device/src/frame_trace.c').read_text()
s=s[s.index('static struct {uint32_t us'):s.index('void __real_lv_display_flush_ready')]
code = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define IRAM_ATTR
static uint32_t clock_us;
static uint64_t esp_timer_get_time(void){return ++clock_us;}
"""+s+r"""
int main(void){
 uint32_t out[2048];
 frame_trace(1,1);assert(frame_trace_take(out,sizeof(out))==0);
 frame_trace_begin(7);frame_trace(1,42);frame_trace(22,0);frame_trace(22,0);
 assert(frame_trace_take(out,sizeof(out))==0);frame_trace_end();
 assert(frame_trace_take(out,8)==0);frame_trace_begin(8);
 size_t n=frame_trace_take(out,sizeof(out));assert(n==36&&out[0]==7&&out[1]==2&&out[2]==0&&out[5]==42);
 assert(frame_trace_take(out,sizeof(out))==0);
 frame_trace_begin(9);for(int i=0;i<700;i++)frame_trace(1,i);frame_trace_end();
 n=frame_trace_take(out,sizeof(out));assert(n==7692&&out[0]==9&&out[1]==640&&out[2]==60);
 puts("Trace gate, bounded overflow, pending ownership, binary length PASS");
}
"""
stage=Path('F:/CABadgeBuild/temp/frame-trace');stage.mkdir(exist_ok=True)
(stage/'test.c').write_text(code)
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(stage/'test.c'),'-o',str(stage/'test.exe')],env=env,check=True)
subprocess.run([str(stage/'test.exe')],check=True)
