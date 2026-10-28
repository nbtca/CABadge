"""Execute production TimeTiles pixel code; mathematical checks, not screen acceptance."""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import hashlib
import os
from pathlib import Path
import subprocess

root = ROOT
out = (TEST_TEMP/'timetiles-raster-test')
out.mkdir(parents=True, exist_ok=True)
source = root/'ui/timetiles'
# Pin the original extraction fingerprint without requiring an untracked outputs copy.
assert hashlib.sha256((source/'timetiles_assets.h').read_bytes()).hexdigest() == '02eb9e8dd0995ead290cc64a4d7c0cda20fc4e1a71521fa5882d1c0aa3847ad8'

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "raster.c"
static struct { uint16_t before[32], pixels[360*360], after[32]; } canvas;
static uint16_t expected[360*360];
static unsigned char alpha[300*32];
static void guards(void) {
    for (int i=0;i<32;i++) assert(canvas.before[i]==0xabcdu && canvas.after[i]==0x1234u);
}
static unsigned rotated(unsigned r, unsigned x, unsigned y) {
    if (r==0) return y*360+x;
    if (r==1) return (359-x)*360+y;
    if (r==2) return (359-y)*360+359-x;
    return x*360+359-y;
}
static uint16_t reference_blend(uint16_t bg,uint16_t fg,unsigned a) {
    unsigned r=((fg>>11)*a+(bg>>11)*(15-a)+7)/15;
    unsigned g=(((fg>>5)&63)*a+((bg>>5)&63)*(15-a)+7)/15;
    unsigned b=((fg&31)*a+(bg&31)*(15-a)+7)/15;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static void mask_check(unsigned id,unsigned rotation,unsigned percent,int x,int y) {
    tt_canvas_t c;
    tt_raster_begin(&c,canvas.pixels,id%6,rotation,false,false);
    for (int i=0;i<360*360;i++) expected[i]=0x0041;
    const tt_mask_meta_t *m=&tt_masks[id];
    unsigned count=0;
    for (unsigned i=m->offset;i<m->offset+m->length;i+=2)
        for (unsigned j=0;j<tt_mask_rle[i];j++) {
            assert(count<sizeof(alpha));alpha[count++]=tt_mask_rle[i+1];
        }
    assert(count==m->width*m->height);
    unsigned w=(m->width*percent+50)/100,h=(m->height*percent+50)/100;
    assert(tt_mask_width(id,percent)==(int)w);
    for (unsigned dy=0;dy<h;dy++) for (unsigned dx=0;dx<w;dx++) {
        int px=x+(int)dx,py=y+(int)dy;
        if (px<0||py<0||px>=360||py>=360) continue;
        unsigned a=alpha[(dy*m->height/h)*m->width+dx*m->width/w];
        expected[rotated(rotation,px,py)]=reference_blend(0x0041,0xf800,a);
    }
    tt_mask(&c,id,x,y,percent,0xf800);
    if (memcmp(canvas.pixels,expected,sizeof(expected))) {
        for (int i=0;i<360*360;i++) if(canvas.pixels[i]!=expected[i]) {
            fprintf(stderr,"mask=%u rotation=%u percent=%u origin=%d,%d first_pixel=%d actual=%04x expected=%04x\n",id,rotation,percent,x,y,i,canvas.pixels[i],expected[i]);break;
        }
        assert(!"mask mismatch");
    }
    guards();
}
int main(void) {
    for (int i=0;i<32;i++) { canvas.before[i]=0xabcd;canvas.after[i]=0x1234; }
    assert(blend565(0x1234,0xabcd,0)==0x1234);
    assert(blend565(0x1234,0xabcd,15)==0xabcd);
    assert(blend565(0,0xffff,7)==0x73ae);
    for (unsigned a=0;a<=15;a++) {
        assert(blend565(0x21a6,0xf800,a)==reference_blend(0x21a6,0xf800,a));
        assert(blend565(0x1234,0x07e0,a)==reference_blend(0x1234,0x07e0,a));
    }
    tt_canvas_t c;
    static const unsigned locations[4][2]={{12,34},{34,347},{347,325},{325,12}};
    for (unsigned r=0;r<4;r++) {
        tt_raster_begin(&c,canvas.pixels,r,r,false,false);
        for (int i=0;i<360*360;i++) assert(canvas.pixels[i]==0x0041);
        tt_rect(&c,12,34,1,1,0xf800);
        assert(canvas.pixels[locations[r][1]*360+locations[r][0]]==0xf800);
        tt_rect(&c,-5,-5,10,10,0x07e0);
        assert(canvas.pixels[rotated(r,0,0)]==0x07e0);
        assert(canvas.pixels[rotated(r,4,4)]==0x07e0);
        assert(canvas.pixels[rotated(r,5,5)]==0x0041);
        tt_rect(&c,355,355,100,100,0x001f);
        tt_rect(&c,INT_MAX,INT_MAX,INT_MAX,INT_MAX,0xffff);
        tt_rect(&c,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0xffff);
        tt_rect(&c,0,0,-1,-1,0xffff);guards();
        for (unsigned id=0;id<119;id++) mask_check(id,r,75,10,20);
    }
    for (unsigned id=0;id<119;id++) {
        mask_check(id,id%4,25,-2,-2);
        mask_check(id,id%4,100,-20,-10);
        mask_check(id,id%4,150,350,355);
    }
    tt_raster_begin(&c,canvas.pixels,0,0,false,false);
    tt_small(&c,"0",100,100,1,0xffff);
    for (unsigned column=0;column<5;column++)
        assert(canvas.pixels[100*360+100+column]==(column>=1&&column<=3?0xffff:0x0041));
    tt_raster_begin(&c,canvas.pixels,0,0,false,false);
    tt_digits(&c,"0:0",100,100,3,0xffff);
    for (unsigned column=0;column<6;column++)
        assert(canvas.pixels[100*360+100+column*3]==(column>=1&&column<=4?0xffff:0x0041));
    assert(canvas.pixels[109*360+121]==0xffff);
    assert(canvas.pixels[121*360+121]==0xffff);
    assert(canvas.pixels[100*360+130]==0xffff);
    tt_raster_begin(&c,canvas.pixels,0,0,false,false);
    tt_icon(&c,6,100,100,1,0xffff);
    assert(canvas.pixels[101*360+103]==0xffff);
    assert(canvas.pixels[101*360+102]==0x0041);
    tt_raster_begin(&c,canvas.pixels,0,0,false,false);
    tt_digits(&c,"0",100,100,6,0xffff);
    uint16_t without_glow=canvas.pixels[99*360+106];
    tt_raster_begin(&c,canvas.pixels,0,0,false,true);
    tt_digits(&c,"0",100,100,6,0xffff);
    assert(canvas.pixels[99*360+106]!=without_glow);
    tt_raster_begin(&c,canvas.pixels,5,3,true,true);
    assert(c.color[6]==0x1060);
    assert(canvas.pixels[rotated(3,10,10)]==0x0041);
    assert(canvas.pixels[rotated(3,18,10)]!=0x0041);
    for (unsigned r=0;r<4;r++) {
        tt_raster_begin(&c,canvas.pixels,0,r,true,true);
        for (unsigned i=0;i<14;i++) tt_icon(&c,i,i%2?-10:350,i%2?-10:350,4,0xabcd);
        tt_small(&c,"0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ :.-",-20,350,3,0xffff);
        tt_digits(&c,"23:59:59",-20,340,6,0xffff);
        tt_mask(&c,999,0,0,75,0xffff);tt_mask(&c,0,0,0,0,0xffff);
        tt_mask(&c,0,INT_MAX,INT_MAX,UINT_MAX,0xffff);
        tt_icon(&c,99,0,0,1,0xffff);tt_icon(&c,0,0,0,0,0xffff);
        tt_small(&c,"0",0,0,0,0xffff);tt_digits(&c,"0",0,0,0,0xffff);
        guards();
    }
    puts("PASS TimeTiles production raster: 119 exact RLE masks, 4 rotations, 25/75/100/150 percent and clipping, native565 alpha, digit/colon/icon fixtures, grid/glow distinction, bounds/canaries");
}
'''
(out/'check.c').write_text(code, encoding='utf-8')
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP), ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe = out/'check.exe'
command = [str(ZIG), 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-UNDEBUG',
           '-I'+str(source), str(out/'check.c'), '-o', str(exe)]
log = root.parent/'outputs/timetiles-port-20261014/raster-check.log'
log.parent.mkdir(parents=True, exist_ok=True)
with log.open('w', encoding='utf-8') as stream:
    stream.write('Executing production raster.c with extracted asset header; no simulator or board acceptance.\n')
    for args in (command, [str(exe)]):
        result = subprocess.run(args, env=env, capture_output=True, text=True)
        stream.write(result.stdout + result.stderr)
        stream.flush()
        print(result.stdout + result.stderr, end='')
        result.check_returncode()
