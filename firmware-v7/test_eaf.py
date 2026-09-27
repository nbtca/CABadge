"""Validate the generated asset and malformed-input guards using firmware code."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/eaf-test'); out.mkdir(parents=True, exist_ok=True)
(out / 'check.c').write_text(r'''
#include "eaf_validate.h"
#include <assert.h>
#include <stdio.h>
static unsigned char data[0x300000];
static void checksum(size_t n){uint32_t sum=0;for(size_t i=16;i<n;i++)sum+=data[i];jx_put32(data+8,sum);}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 size_t n=fread(data,1,sizeof(data),f);fclose(f);assert(eaf_validate(data,n));
 assert(!eaf_validate(data,0)&&!eaf_validate(data,16)&&!eaf_validate(data,n-1));
 unsigned base=16+jx_u32(data+4)*8,header=base+2;
 data[header+10]=0;checksum(n);assert(!eaf_validate(data,n)); /* dimension */
 data[header+10]=360&255;checksum(n);assert(eaf_validate(data,n));
 data[header+16]=0;checksum(n);assert(!eaf_validate(data,n)); /* zero block height */
 data[header+16]=20;checksum(n);assert(eaf_validate(data,n));
 jx_put32(data+20,0xffffffffu);checksum(n);assert(!eaf_validate(data,n)); /* offset */
 puts("PASS: test EAF, dimensions, truncated file, checksum/table boundaries, block height");
}
''')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe = out / 'check.exe'
subprocess.run([str(root / '.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG',
                '-I' + str(root / 'usb_screen'), '-I' + str(root / 'usb_screen/device/src'),
                str(out / 'check.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe), str(root / 'assets/wallpapers/live_test.eaf')], check=True)

full=Path('F:/CABadgeBuild/wallpapers/endfield/endfield_full_oversize.eaf')
if full.exists(): subprocess.run([str(exe),str(full)],check=True)
