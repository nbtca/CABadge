"""Flash timing metadata persistence, frame indexing and deadline check."""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent/'tools'))
from eaf_timing import make, read

out = Path('F:/CABadgeBuild/temp/eaf-timing-check'); out.mkdir(parents=True, exist_ok=True)
# Metadata validation needs only the EAF signature/count; decoder tests cover pixels.
raw = b'\x89EAF' + struct.pack('<I', 3)
metadata = make(raw, [80000, 180000, 420000])
info = read(metadata, raw)
assert info['mode'] == 'PER_FRAME' and info['delays_us'] == [80000, 180000, 420000]
assert read(make(raw, [33333]*3), raw)['delays_us'] == [33333]*3
assert read(make(raw, [None, 0, 0]), raw)['delays_us'] == [33333]*3
assert read(make(raw, [1, 999999999, 0]), raw)['delays_us'] == [10000, 60000000, 33000]
try:
    read(metadata[:-1], raw)
    raise AssertionError('truncated table accepted')
except ValueError:
    pass

production = (root/'usb_screen/device/src/eaf_wallpaper.c').read_text()
callback = re.search(r'static bool next_frame\([^}]+\}', production).group()
scheduler = (root/'ui/ui_transition_compositor.c').read_text()
deadline = re.search(r'due\+=duration_us;[^\n]+', scheduler).group()
code = r'''
#include "wallpaper_store.h"
#include "wallpaper_timing.h"
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
static uint8_t flash[WALL_LIBRARY_END];
bool wall_read(uint32_t o,void *p,size_t n){assert(o+n<=sizeof(flash));memcpy(p,flash+o,n);return true;}
bool wall_write(uint32_t o,const void *p,size_t n){assert(o+n<=sizeof(flash));const uint8_t *b=p;for(size_t i=0;i<n;i++){assert((flash[o+i]&b[i])==b[i]);flash[o+i]&=b[i];}return true;}
bool wall_erase(uint32_t o,size_t n){assert(o+n<=sizeof(flash));memset(flash+o,255,n);return true;}
typedef struct {const uint8_t *timing;atomic_uint frame;unsigned frames;bool first;} eaf_backend_t;
static bool decode(eaf_backend_t *b,unsigned i,bool native){atomic_store(&b->frame,i);return true;}
''' + callback + r'''
int main(void){
 uint8_t meta[]={META};uint32_t crc=jx_u32(meta+20);size_t n=sizeof(meta);
 assert(wall_timing_valid(meta,n,3,crc));assert(!wall_timing_valid(meta,n,4,crc));
 assert(!wall_timing_valid(meta,n,3,crc^1));assert(!wall_timing_valid(meta,n-1,3,crc));
 meta[n-1]^=1;assert(!wall_timing_valid(meta,n,3,crc));meta[n-1]^=1;
 assert(wall_timing_period(NULL,0)==33333);
 memset(flash,255,sizeof(flash));wall_record_t records[WALL_LIBRARY_COUNT],pending;
 assert(wall_library_scan(records));
 assert(wall_library_reserve(records,4096,crc,&pending));
 assert(wall_library_write_timing(&pending,meta,n));
 assert(wall_library_publish(records,&pending));
 assert(wall_library_scan(records)&&records[pending.slot].timing_crc==jx_u32(meta+24));
 assert(!memcmp(flash+WALL_LIBRARY_BASE+pending.slot*WALL_LIBRARY_SLOT+64,meta,n));
 assert(wall_library_delete(records,pending.slot)&&wall_library_scan(records));
 assert(records[pending.slot].slot==-1);
 assert(wall_library_reserve(records,4096,crc,&pending)&&wall_library_publish(records,&pending));
 assert(wall_library_scan(records)&&records[pending.slot].timing_crc==0); /* legacy */
 eaf_backend_t b={.timing=meta,.frames=3,.first=true};
 int64_t due=0;uint32_t duration_us=0;
 unsigned periods[]={80000,180000,420000};
 for(unsigned i=0;i<6;i++){
  assert(next_frame(&b,&duration_us)&&atomic_load(&b.frame)==i%3&&duration_us==periods[i%3]);
  int64_t start=due,now=start+25000; /* decode + DMA costs are inside deadline */
  DEADLINE
  assert(due==start+periods[i%3]);
 }
 b.first=true;assert(next_frame(&b,&duration_us)&&atomic_load(&b.frame)==2&&duration_us==420000); /* resume */
 puts("PASS: metadata CRC/count/bounds, Flash reboot/delete/legacy, frame duration, absolute deadlines/resume");
}
'''
code = code.replace('META', ','.join(map(str, metadata))).replace('DEADLINE', deadline)
(out/'check.c').write_text(code)
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp', ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe = out/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG', '-I'+str(root/'usb_screen'),
                str(out/'check.c'), str(root/'usb_screen/wallpaper_store.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
