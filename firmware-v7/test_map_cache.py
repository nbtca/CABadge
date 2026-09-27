"""Check real cache LRU/pins and progressive rectangle sampling, without a UI benchmark."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/map-cache-test')
out.mkdir(parents=True, exist_ok=True)
(out / 'check.c').write_text(r'''
#include "map_cache.h"
#include "map_lane_policy.h"
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
static uint16_t *pixels(void){uint16_t *p=malloc(128*128*2);assert(p);for(int i=0;i<128*128;i++)p[i]=i;return p;}
int main(void){
 /* Warm primary no longer has to recover 72/64 KiB to admit a new lane. */
 assert(!map_lane_memory_reason(59803,52015,5828520,20000,true,true));
 assert(!map_lane_memory_reason(39828,32040,4420000,16000,false,false));
 assert(!strcmp(map_lane_memory_reason(30000,40000,4000000,20000,false,false),"INTERNAL_RESERVE"));
 assert(!strcmp(map_lane_memory_reason(60000,23000,4000000,20000,false,false),"DMA_RESERVE"));
 assert(!strcmp(map_lane_memory_reason(80000,70000,4000000,8000,true,true),"INTERNAL_FRAGMENTED"));
 assert(!strcmp(map_lane_memory_reason(80000,70000,400000,20000,false,false),"PSRAM_RESERVE"));
 assert(map_lane_memory_reason(50000,45000,4000000,20000,true,true));
 map_cache_t c={0};map_key_t a={0,1,0,0};
 assert(map_cache_put(&c,a,pixels(),1,true));
 for(int i=1;i<MAP_CACHE_COUNT;i++)assert(map_cache_put(&c,(map_key_t){0,1,i,0},pixels(),i+1,false));
 assert(map_cache_find(&c,(map_key_t){0,1,1,0},100));
 assert(map_cache_put(&c,(map_key_t){1,1,0,0},pixels(),101,false));
 assert(map_cache_find(&c,a,102)); /* pinned */
 assert(map_cache_find(&c,(map_key_t){0,1,1,0},102)); /* recently used */
 assert(!map_cache_find(&c,(map_key_t){0,1,2,0},102)); /* cold LRU */
 assert(!map_cache_find(&c,(map_key_t){0,2,0,0},102)); /* LOD distinct */
 assert(map_cache_find(&c,(map_key_t){1,1,0,0},102)); /* world distinct */
 for(int i=0;i<MAP_CACHE_COUNT;i++)c.tile[i].pinned=true;
 uint16_t *p=pixels();assert(!map_cache_put(&c,(map_key_t){2,3,9,9},p,103,false));free(p);
 /* Page/HTTP teardown releases pins, not successful decoded pixels. */
 map_cache_unpin(&c);
 map_cached_tile_t *kept=map_cache_find(&c,a,104);assert(kept&&!kept->pinned&&kept->updated==1);
 assert(map_cache_evict_cold(&c));assert(!map_cache_find(&c,(map_key_t){0,1,3,0},105));
 map_cache_clear(&c);
 const float scales[]={1,2,5,10,25,50};
 for(int zoom=0;zoom<6;zoom++)for(int sign=-1;sign<=1;sign+=2){
  float cx=sign*48,cz=sign*96,bpp=scales[zoom];int lod=zoom/2+1;
  float size=lod==1?500:lod==2?2500:12500;
  int coverage[360*360]={0};
  for(int tz=(int)floorf((cz-180*bpp)/size);tz<=(int)floorf((cz+179*bpp)/size);tz++)
  for(int tx=(int)floorf((cx-180*bpp)/size);tx<=(int)floorf((cx+179*bpp)/size);tx++){
   map_cached_tile_t t={.key={0,lod,tx,tz},.pixels=pixels()};int x,y,w,h;
   map_tile_bounds(t.key,cx,cz,bpp,&x,&y,&w,&h);assert(w&&h&&x>=0&&y>=0&&x+w<=360&&y+h<=360);
   uint16_t patch[360*16];
   for(int row=0;row<h;row+=16){int height=h-row<16?h-row:16;
    map_tile_sample(&t,cx,cz,bpp,x,y+row,w,height,patch,w);
    for(int dy=0;dy<height;dy++)for(int dx=0;dx<w;dx++){
     int sx=x+dx,sy=y+row+dy;
     int px=(int)((cx+(sx-180)*bpp-tx*size)/size*128),pz=(int)((cz+(sy-180)*bpp-tz*size)/size*128);
     assert(patch[dy*w+dx]==t.pixels[pz*128+px]);coverage[sy*360+sx]++;
    }
   }free(t.pixels);
  }for(int i=0;i<360*360;i++)assert(coverage[i]==1);
 }
 puts("PASS: bounded twenty tiles, world/LOD keys, LRU/pin ownership, all six scales and negative-coordinate patch coverage");
}
''')
src = root / 'usb_screen/device/src'
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe = out / 'check.exe'
subprocess.run([str(root / '.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG',
                '-I' + str(src), str(out / 'check.c'), str(src / 'map_cache.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
