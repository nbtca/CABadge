"""Production streaming decoder: fragmented input, sampling parity, CRC/truncation.
No simulator or board benchmark. All outputs live on F:.
"""
import os, struct, subprocess, zlib, sys
from pathlib import Path

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/map-stream-test')
out.mkdir(exist_ok=True)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
raw = b''.join(b'\0' + bytes(v for x in range(501) for v in (x % 256, y % 256, (x+y) % 256, 0 if (x+y) % 11 == 0 else 255)) for y in range(1002))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',501,1002,8,6,0,0,0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND',b'')
(out/'tile.png').write_bytes(png)
# Also accept a real server tile for the identical sampling check.
from PIL import Image
source = Path(sys.argv[1]) if len(sys.argv)>1 else out/'tile.png'
im = Image.open(source).convert('RGBA')
expected=[]
for y in range(128):
    for x in range(128):
        r,g,b,a=im.getpixel((x*500//128,y*500//128))
        expected.append(((r>>3)<<11)|((g>>2)<<5)|(b>>3) if a else 0x1082)
(out/'expected.bin').write_bytes(struct.pack('<16384H',*expected))
(out/'check.c').write_text(r'''
#include "map_png.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
    unsigned char *input=malloc(size);assert(fread(input,1,size,f)==size);fclose(f);
    uint16_t pixels[128*128],expected[128*128];f=fopen(argv[2],"rb");assert(f&&fread(expected,2,128*128,f)==128*128);fclose(f);int chunks[]={1,7,1024,16384};
    for(int run=0;run<6;run++){
        if(run==5)input[size-1]^=1;
        map_png_t *m=map_png_create(pixels);assert(m);unsigned char buf[16384];size_t pos=0,used=0,limit=run==4?size-5:size;int error=0;
        while(pos<limit){
            size_t n=run<4?chunks[run]:1024;if(n>sizeof(buf)-used)n=sizeof(buf)-used;if(n>limit-pos)n=limit-pos;
            assert(n);memcpy(buf+used,input+pos,n);pos+=n;used+=n;
            int fed=map_png_feed(m,buf,used);if(fed<0){error=1;break;}used-=fed;memmove(buf,buf+fed,used);
        }
        if(run<4){assert(!error&&map_png_done(m));
            assert(!memcmp(pixels,expected,sizeof(pixels)));
        }else assert(error||!map_png_done(m));
        map_png_destroy(m);
    }
    free(input);puts("PASS: fragmented stream, exact RGB565 samples, truncated/CRC-invalid rejected");
}
''')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp', ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
src = root/'usb_screen/device/src'
exe = out/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc','-O2','-I'+str(src),str(out/'check.c'),str(src/'map_png.c'),str(src/'pngle/miniz.c'),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe),str(source),str(out/'expected.bin')],check=True)
