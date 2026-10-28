"""Production FluidBox math and five raster modes; off-board checks, not display acceptance."""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
from pathlib import Path
import subprocess

root = ROOT
out=(TEST_TEMP/'fluidbox-test')
(out/'freertos').mkdir(parents=True,exist_ok=True)
(out/'esp_log.h').write_text('#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n')
(out/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
(out/'freertos/FreeRTOS.h').write_text('#include <assert.h>\n#define configASSERT assert\n#define portMAX_DELAY 0\n')
(out/'freertos/semphr.h').write_text('typedef int SemaphoreHandle_t;\nstatic inline int xSemaphoreCreateMutex(void){return 1;}\nstatic inline int xSemaphoreTake(int a,int b){return 1;}\nstatic inline int xSemaphoreGive(int a){return 1;}\n')
code=r'''
#include "config.h"
#include "input.h"
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
int64_t esp_timer_get_time(void){return (int64_t)clock()*1000000/CLOCKS_PER_SEC;}
static struct { uint64_t before;uint16_t pixels[LCD_H_RES*LCD_V_RES];uint64_t after; } canvas;
static unsigned bands;
void display_stage_band(int band,int left,int right,const uint16_t *pixels){
 assert(band>=0&&band<BAND_COUNT&&left>=0&&right<=LCD_H_RES&&right>left);
 for(int y=0;y<BAND_ROWS;y++)memcpy(canvas.pixels+(band*BAND_ROWS+y)*LCD_H_RES+left,pixels+y*(right-left),(right-left)*2);
 bands++;
}
void display_present(void){}
static void particles(void){
 sim_particle_view_t p[PARTICLE_MAX];int n=sim_snapshot(p,PARTICLE_MAX);assert(n==PARTICLE_COUNT);
 for(int i=0;i<n;i++){
  assert(isfinite(p[i].x)&&isfinite(p[i].y)&&isfinite(p[i].z)&&isfinite(p[i].speed));
  assert(hypotf(p[i].x-BOX_W*.5f,p[i].y-BOX_H*.5f)<=BOX_W*.5f-3.75f+.01f);
  assert(p[i].z>=3.7f&&p[i].z<=BOX_D-3.7f);
 }
}
int main(void){
 for(int axis=0;axis<3;axis++)for(int sign=-1;sign<=1;sign+=2){
  fluidbox_input_t input={0};int16_t mg[3]={0};mg[axis]=1000*sign;
  sim_forces_t f=fluidbox_input(&input,mg,.01f);
  assert(f.gravity[axis]*sign>200000);
  for(int i=0;i<3;i++){assert(f.omega[i]==0&&f.alpha[i]==0);if(i!=axis)assert(f.gravity[i]==0);}
 }
 fluidbox_input_t input={0};int16_t zero[3]={0},up[3]={0,0,1000},shake[3]={2000,-2000,2000};
 sim_forces_t f=fluidbox_input(&input,zero,.01f);assert(f.gravity[1]>0);
 f=fluidbox_input(&input,up,.5f);assert(f.gravity[2]>0);
 f=fluidbox_input(&input,shake,.01f);assert(f.gravity[0]>0&&f.gravity[1]<0);
 for(int i=0;i<3;i++)assert(isfinite(f.gravity[i])&&fabsf(f.gravity[i])<600000);
 sim_init();render_init();canvas.before=canvas.after=0x1234567812345678ULL;
 for(int mode=0;mode<RENDER_MODE_COUNT;mode++){
  sim_reset();render_set_mode(mode);
  for(int axis=0;axis<3;axis++)for(int sign=-1;sign<=1;sign+=2){
   f=(sim_forces_t){0};f.gravity[axis]=GRAVITY_GAIN*GRAVITY_MPS2*PX_PER_METER*sign;
   render_set_gravity(f.gravity);
   for(int i=0;i<120;i++)sim_step(.007f,&f);
   particles();
   for(int width=0;width<2;width++){
    bands=0;render_stats_t st=render_frame(width);assert(bands&&st.bytes<=sizeof(canvas.pixels));
    assert(canvas.before==0x1234567812345678ULL&&canvas.after==0x1234567812345678ULL);
   }
  }
 }
 puts("PASS FluidBox: 1200 finite particles, round/depth walls, six gravity directions, shake/free-fall, no gyro, five modes, 360px band bounds/canaries");
}
'''
(out/'check.c').write_text(code,encoding='utf-8')
env=dict(os.environ,TEMP=str(TEST_TEMP),TMP=str(TEST_TEMP),ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe=out/'check.exe'
sources=root/'ui/fluidbox'
subprocess.run([str(ZIG),'cc','-O2','-UNDEBUG',
    '-I'+str(out),'-I'+str(sources),str(out/'check.c'),str(sources/'sim.c'),str(sources/'render.c'),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
