"""Compare production EAF decode with the pinned official block decoder on real assets."""
import os
import re
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
component = Path('F:/CABadgeBuild/staging/usb-screen-v7-idf61/managed_components/espressif__esp_lv_eaf_player/src')
out = Path('F:/CABadgeBuild/temp/eaf-direct-check'); out.mkdir(parents=True, exist_ok=True)
official = (component/'esp_eaf_dec.c').read_text()
production = (root/'usb_screen/device/src/eaf_wallpaper.c').read_text()
def function(source, name):
    start = re.search(r'^\w[^\n]*\b'+name+r'\(', source, re.M).start()
    end = source.index('{', start)+1
    depth = 1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}')
        end += 1
    return source[start:end]+'\n'
(out/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_INVALID_ARG -3\n')
code = r'''
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include "esp_eaf_dec.h"
#include "eaf_validate.h"
#include "wallpaper_timing.h"
#include "ui_transition_compositor.h"
#define CABADGE_EAF_INTERPOLATE 1
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_malloc(n,c) malloc(n)
static int64_t esp_timer_get_time(void){static int64_t t;return ++t;} /* Logic only, never MCU timing evidence. */
#define ESP_LOGE(...) ((void)0)
#define esp_eaf_malloc_prefer_psram malloc
static esp_eaf_block_decoder_t s_esp_eaf_decoder[ESP_EAF_ENCODING_MAX]={esp_eaf_rle_decode};
static uint8_t file[0x300000];
const uint8_t *esp_eaf_format_get_frame_data(esp_eaf_format_handle_t h,int i){
 return file+16+jx_u32(file+4)*8+jx_u32(file+20+i*8)+2;
}
int esp_eaf_format_get_frame_size(esp_eaf_format_handle_t h,int i){return jx_u32(file+16+i*8)-2;}
typedef struct {const uint8_t *data;} lv_image_dsc_t;
'''
a = production.index('typedef struct {'); b = production.index('} eaf_backend_t;',a)+len('} eaf_backend_t;')
code += production[a:b]+'\n'
for name in ['esp_eaf_header_parse','esp_eaf_free_header','esp_eaf_calculate_offsets','esp_eaf_palette_get_color','esp_eaf_rgb565_from_bgr','esp_eaf_get_block_rows','esp_eaf_block_decode','esp_eaf_rle_decode']:
    code += function(official, name)
for name in ['decode_into', 'decode', 'midpoint', 'configure_interpolation', 'interpolated_frame', 'next_frame']:
    code += function(production, name)
code += r'''
static uint16_t reference_mean(uint16_t a,uint16_t b){
 a=__builtin_bswap16(a);b=__builtin_bswap16(b);
 unsigned r=(((a>>11)&31)+((b>>11)&31))/2,g=(((a>>5)&63)+((b>>5)&63))/2,blue=((a&31)+(b&31))/2;
 return __builtin_bswap16((r<<11)|(g<<5)|blue);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);size_t n=fread(file,1,sizeof(file),f);fclose(f);assert(eaf_validate(file,n));
 eaf_backend_t b={0};static uint16_t actual[360*360],expected[360*360],following[360*360];b.picture.data=(const uint8_t*)actual;b.pixels=(uint8_t*)actual;
 unsigned total=jx_u32(file+4);
 for(unsigned frame=0;frame<total;frame++){
  for(unsigned swap=0;swap<2;swap++){
   assert(decode(&b,frame,swap));assert(b.native==swap&&atomic_load(&b.frame)==frame);
   esp_eaf_header_t h={0};const uint8_t *data=esp_eaf_format_get_frame_data(NULL,frame);
   assert(esp_eaf_header_parse(data,esp_eaf_format_get_frame_size(NULL,frame),&h)==ESP_EAF_FORMAT_VALID);
   esp_eaf_palette_cache_t cache={0};memset(cache.color,255,sizeof(cache.color));
   for(unsigned block=0;block<h.blocks;block++)assert(esp_eaf_block_decode(&h,data,block,(uint8_t*)(expected+block*h.block_height*360),NULL,swap,&cache)==ESP_OK);
   assert(!memcmp(actual,expected,sizeof(actual)));esp_eaf_free_header(&h);
  }
 }
 printf("PASS: %u real frames match official RGB565 decode, CPU and LCD byte order\n",total);
 for(unsigned i=0;i<65536;i++){
  uint16_t x=(uint16_t)i,y=(uint16_t)(i*40503u+97u),out[3]={0xa55a,0,0x5aa5};
  midpoint(out+1,&x,&y,1);assert(out[1]==reference_mean(x,y)&&out[0]==0xa55a&&out[2]==0x5aa5);
  midpoint(out+1,&x,&x,1);assert(out[1]==x);
 }
 b.frames=total;assert(decode(&b,0,true)&&configure_interpolation(&b,true));b.first=true;
 uint64_t duration=0;ui_compositor_frame_t frame={0};uint32_t period;
 for(unsigned i=0;i<total*2;i++){
  assert(next_frame(&b,&frame,&period));unsigned index=i/2;duration+=period;
  assert(atomic_load(&b.frame)==index&&frame.interpolated==(bool)(i&1));
  assert(period==(i&1?16667:16666)&&frame.layers[0].pixels==b.picture.data);
  assert(frame.source_decode_us==(i&1)&&frame.blend_us==(i&1));
  assert(decode_into(&b,index,true,(uint8_t*)expected));
  if(i&1){
   assert(decode_into(&b,(index+1)%total,true,(uint8_t*)following));
   for(unsigned p=0;p<360u*360u;p++)assert(((const uint16_t*)b.picture.data)[p]==reference_mean(expected[p],following[p]));
  }else assert(!memcmp(b.picture.data,expected,sizeof(expected)));
 }
 assert(duration==(uint64_t)total*33333);
 assert(next_frame(&b,&frame,&period)&&atomic_load(&b.frame)==0&&!frame.interpolated); /* loop */
 assert(next_frame(&b,&frame,&period)&&frame.interpolated);b.first=true;
 assert(next_frame(&b,&frame,&period)&&atomic_load(&b.frame)==0&&!frame.interpolated); /* joined resume */
 assert(configure_interpolation(&b,false)&&b.picture.data==b.pixels);b.first=true;
 assert(next_frame(&b,&frame,&period)&&period==33333&&!frame.interpolated&&frame.blend_us==0);
 assert(decode_into(&b,0,true,(uint8_t*)expected)&&!memcmp(b.picture.data,expected,sizeof(expected)));
 uint8_t meta[WALL_TIMING_MAX]={0};unsigned ms[]={80,180,420};memcpy(meta,"JXWT",4);meta[4]=1;meta[6]=2;
 jx_put32(meta+8,total);jx_put32(meta+12,33333);jx_put32(meta+16,total*2);
 for(unsigned i=0;i<total;i++){meta[28+i*2]=ms[i%3]&255;meta[29+i*2]=ms[i%3]>>8;}
 jx_put32(meta+24,~jx_crc(jx_crc(~0u,meta,24),meta+28,total*2));
 assert(wall_timing_valid(meta,28+total*2,total,0));b.timing=meta;b.first=true;
 assert(configure_interpolation(&b,true));duration=0;
 for(unsigned i=0;i<12;i++){assert(next_frame(&b,&frame,&period));assert(period==ms[(i/2)%3]*500);duration+=period;}
 assert(duration==1360000); /* Variable holds also preserve authored time, not merely frame count. */
 free(b.source[0]);free(b.source[1]);
 printf("PASS: %u originals + %u exact channel midpoints, byte order/guards/identity, timing/loop/resume/OFF\n",total,total);
}
'''
(out/'check.c').write_text(code)
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe=out/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc','-O2','-UNDEBUG','-I'+str(out),'-I'+str(component),'-I'+str(root/'ui'),'-I'+str(root/'usb_screen'),'-I'+str(root/'usb_screen/device/src'),str(out/'check.c'),'-o',str(exe)],env=env,check=True)
current = Path('F:/CABadgeBuild/wallpapers/纯享版的罗小黑，超可爱的大屏表情。_20260927_204902_y22arr_g/纯享版的罗小黑，超可爱的大屏表情。_01.eaf')
assert current.exists(), 'current 300-frame LuoXiaohei asset missing'
for asset in [root/'assets/wallpapers/live_test.eaf',Path('F:/CABadgeBuild/wallpapers/luoxiaohei/luoxiaohei_01.eaf'),current]:
    if asset.exists():subprocess.run([str(exe),str(asset)],check=True)
