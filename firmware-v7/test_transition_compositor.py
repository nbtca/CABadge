"""Exercise actual RGB565 row packer; no claim about physical scan tearing."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
stage=Path('F:/CABadgeBuild/temp/compositor-check');stage.mkdir(exist_ok=True)
check=r"""
#include "ui_transition_compositor.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t a[40*23],b[48*19],out[30*25*2];
static int inside(const ui_compositor_layer_t *l,int x,int y){
 x-=l->x;y-=l->y;if(x<0||y<0||x>=l->width||y>=l->height)return 0;
 float r=l->radius;if(r>l->width/2)r=l->width/2;if(r>l->height/2)r=l->height/2;
 float px=x+.5f,py=y+.5f,cx=px<r?r:px>l->width-r?l->width-r:px,cy=py<r?r:py>l->height-r?l->height-r:py;
 return (px-cx)*(px-cx)+(py-cy)*(py-cy)<=r*r;
}
int main(void){
 for(unsigned i=0;i<sizeof(a);i++)a[i]=(uint8_t)(i*13);for(unsigned i=0;i<sizeof(b);i++)b[i]=(uint8_t)(i*7+3);
 for(int dx=-25;dx<26;dx++)for(int dy=-22;dy<23;dy++){
  ui_compositor_frame_t f={.x=3,.y=4,.width=25,.height=20,.count=2,.background=0x3587,
   .layers={{a,40,17,23,dx,dy,0},{b,48,20,19,dx+9,dy+7,7}}};
  memset(out,0xcc,sizeof(out));ui_transition_compositor_rows(&f,3,11,out);
  for(int y=0;y<11;y++)for(int x=0;x<25;x++){
   const uint8_t *expected=(const uint8_t*)&f.background;
   for(int i=0;i<2;i++)if(inside(&f.layers[i],x+3,y+7))expected=f.layers[i].pixels+(y+7-f.layers[i].y)*f.layers[i].stride+(x+3-f.layers[i].x)*2;
   assert(!memcmp(out+(y*25+x)*2,expected,2));
  }
  for(unsigned i=25*11*2;i<sizeof(out);i++)assert(out[i]==0xcc);
 }
 puts("Production compositor: stride, horizontal/vertical clipping, rounded overlay, block offset and bounds PASS");
}
"""
source=(root/'ui/ui_transition_compositor.c').read_text()
a=source.index('if(atomic_load(&cancelled)){');end=a+source[a:].index('{');depth=1;end+=1
while depth:
 if source[end]=='{':depth+=1
 elif source[end]=='}':depth-=1
 end+=1
branch=source[a:end]
check=check.replace(' puts("Production compositor:', '''
 int acknowledgements=0;bool cancelled=true;
 #define atomic_load(p) (*(p))
 #define physical_display_transition_drain() ((void)0)
 #define xSemaphoreGive(s) (++acknowledgements)
 ui_compositor_frame_t queued[2]={{.count=2},{.count=0}};
 for(int i=0;i<2;i++){ui_compositor_frame_t f=queued[i];
 '''+branch+'''
 }
 assert(acknowledgements==1); /* ordinary queued frame + STOP must not double-ack */
 puts("Production compositor:''')
(stage/'check.c').write_text(check)
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc','-I'+str(root/'ui'),str(stage/'check.c'),str(root/'ui/ui_transition_compositor.c'),'-o',str(stage/'check.exe')],env=env,check=True)
subprocess.run([str(stage/'check.exe')],check=True)
