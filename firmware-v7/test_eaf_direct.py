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
code += function(production,'decode')
code += r'''
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);size_t n=fread(file,1,sizeof(file),f);fclose(f);assert(eaf_validate(file,n));
 eaf_backend_t b={0};uint16_t actual[360*360],expected[360*360];b.picture.data=(const uint8_t*)actual;
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
}
'''
(out/'check.c').write_text(code)
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe=out/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc','-O2','-I'+str(out),'-I'+str(component),'-I'+str(root/'usb_screen'),'-I'+str(root/'usb_screen/device/src'),str(out/'check.c'),'-o',str(exe)],env=env,check=True)
for asset in [root/'assets/wallpapers/live_test.eaf',Path('F:/CABadgeBuild/wallpapers/luoxiaohei/luoxiaohei_01.eaf')]:
    if asset.exists():subprocess.run([str(exe),str(asset)],check=True)
