"""Check the real Flash library: ordinary artwork survives reboot and deletion stays deleted."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/wallpaper-library-test')
out.mkdir(parents=True, exist_ok=True)
(out / 'check.c').write_text(r'''
#include "wallpaper_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char flash[WALL_LIBRARY_END],pixels[WALL_BYTES],copy[WALL_BYTES];
bool wall_read(uint32_t off,void *p,size_t n){if(off+n>sizeof(flash))return false;memcpy(p,flash+off,n);return true;}
bool wall_write(uint32_t off,const void *p,size_t n){if(off+n>sizeof(flash))return false;memcpy(flash+off,p,n);return true;}
bool wall_erase(uint32_t off,size_t n){if(off+n>sizeof(flash))return false;memset(flash+off,255,n);return true;}
int main(void){
 memset(flash,255,sizeof(flash));wall_record_t records[WALL_LIBRARY_COUNT];
 assert(wall_library_scan(records)&&wall_library_migrate(records,copy));
 int a,b;memset(pixels,17,sizeof(pixels));
 assert(wall_library_add(records,pixels,wall_crc(pixels,sizeof(pixels)),&a));
 memset(pixels,29,sizeof(pixels));
 assert(wall_library_add(records,pixels,wall_crc(pixels,sizeof(pixels)),&b)&&a!=b);
 assert(wall_library_scan(records)&&wall_library_read(&records[b],copy)&&!memcmp(copy,pixels,sizeof(copy)));
 assert(wall_library_delete(records,a));
 assert(wall_library_scan(records)&&wall_library_migrate(records,copy));
 assert(records[a].slot==-1&&records[b].slot==b); /* no resurrection */
 assert(wall_library_delete(records,b));
 assert(wall_library_scan(records)&&wall_library_migrate(records,copy));
 for(int i=0;i<WALL_LIBRARY_COUNT;i++)assert(records[i].slot==-1);
 assert(wall_library_add(records,pixels,wall_crc(pixels,sizeof(pixels)),&a));
 assert(wall_library_read(&records[a],copy)&&!memcmp(copy,pixels,sizeof(copy)));
 /* Three MiB extent, safe adjacent static data, reboot, deletion and interrupted upload. */
 static unsigned char large[WALL_EAF_MAX],large_copy[WALL_EAF_MAX];
 memset(large,0x5c,sizeof(large));wall_record_t pending;
 assert(wall_library_find(records,WALL_EAF_MAX+1)==-1);
 assert(wall_library_reserve(records,sizeof(large),wall_crc(large,sizeof(large)),&pending));
 assert(!wall_library_write_chunk(&pending,sizeof(large),large,1));
 for(unsigned o=0;o<sizeof(large);o+=1024)assert(wall_library_write_chunk(&pending,o,large+o,1024));
 assert(wall_library_publish(records,&pending));
 assert(wall_library_scan(records)&&wall_library_read(&records[pending.slot],large_copy)&&!memcmp(large,large_copy,sizeof(large)));
 assert(wall_library_read(&records[a],copy)&&!memcmp(copy,pixels,sizeof(copy)));
 assert(records[pending.slot+1].slot==-2);
 assert(wall_library_add(records,pixels,wall_crc(pixels,sizeof(pixels)),&b)&&b>pending.slot+1);
 assert(wall_library_delete(records,pending.slot)&&wall_library_scan(records));
 assert(records[pending.slot].slot==-1&&records[b].slot==b);
 assert(wall_library_reserve(records,sizeof(large),wall_crc(large,sizeof(large)),&pending));
 assert(wall_library_write_chunk(&pending,0,large,1024));
 assert(wall_library_scan(records)&&records[pending.slot].slot==-1&&records[a].slot==a&&records[b].slot==b);
 puts("PASS: 3MiB streaming extents, reboot/cancel cleanup, static preservation");
 puts("PASS: one library, persistence, delete, empty library reboot, re-upload");
}
''')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe = out / 'check.exe'
subprocess.run([str(root / '.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG',
                '-I' + str(root / 'usb_screen'), str(out / 'check.c'),
                str(root / 'usb_screen/wallpaper_store.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
