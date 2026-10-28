"""Production touch/effect checks; no simulator or physical acceptance."""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
from pathlib import Path
import subprocess

root = ROOT
src = root/'ui/timetiles'
out = (TEST_TEMP/'timetiles-effects-test')
out.mkdir(parents=True, exist_ok=True)
code = r'''
#include "effects.h"
#include <assert.h>
#include <stdio.h>
static struct {unsigned before;tt_effect_t e;unsigned after;} state={.before=123,.after=456};
static struct {unsigned before;uint16_t pixels[360*360];unsigned after;} image={.before=789,.after=987};
static void guards(void){assert(state.before==123&&state.after==456&&image.before==789&&image.after==987);}
static void tap(tt_effect_t *e,int x,int y,uint64_t now){tt_effect_pointer(e,0,x,y,now);tt_effect_pointer(e,2,x,y,now+50);}
int main(void){
 tt_effect_t *e=&state.e;tt_canvas_t c;tt_effect_clock(e,"12:34");assert(e->count>0&&e->count<336);
 int x=e->tiles[0].home_x+2,y=e->tiles[0].home_y+2;
 tt_effect_pointer(e,0,x,y,100);tt_effect_pointer(e,1,x+20,y,101);assert(e->moved);
 for(unsigned t=116;t<600;t+=16)tt_effect_tick(e,t);
 assert(e->mode==0&&e->active&&e->radius>0);bool changed=false;
 for(unsigned i=0;i<e->count;i++)if(e->tiles[i].x!=e->tiles[i].home_x*256)changed=true;
 assert(changed);tt_effect_pointer(e,2,x+20,y,601);
 for(unsigned t=617;t<6500;t+=16)tt_effect_tick(e,t);
 assert(!e->held&&!e->active&&!e->radius);
 for(unsigned i=0;i<e->count;i++)assert(e->tiles[i].x==e->tiles[i].home_x*256&&e->tiles[i].y==e->tiles[i].home_y*256&&!e->tiles[i].vx&&!e->tiles[i].vy);
 tt_effect_cancel(e);tap(e,180,180,7000);tap(e,180,180,7200);assert(e->mode==0&&!e->theme_advance);
 tt_effect_tick(e,7250+TT_TAP_GAP-1);assert(e->mode==0);
 tt_effect_tick(e,7250+TT_TAP_GAP);assert(e->mode==TT_EFFECT_NOVA&&e->theme_advance);
 e->theme_advance=false;tt_effect_tick(e,9900);assert(e->mode==0&&!e->theme_advance);
 tap(e,180,180,10000);tap(e,180,180,10500);assert(e->mode==0);
 tt_effect_cancel(e);tap(e,130,145,11000);tap(e,200,145,11200);assert(e->mode==0);
 tt_effect_cancel(e);tt_effect_pointer(e,0,180,170,12000);
 tt_effect_tick(e,12679);assert(e->mode==0);tt_effect_tick(e,12680);assert(e->mode==TT_EFFECT_MOSAIC&&e->long_press);
 tt_effect_pointer(e,1,220,180,12700);assert(e->x==220&&e->y==180);
 tt_effect_pointer(e,2,220,180,12800);tt_effect_tick(e,13479);assert(e->mode==TT_EFFECT_MOSAIC);
 tt_effect_tick(e,13480);assert(e->mode==0&&!e->held);
 tt_effect_pointer(e,0,180,170,14000);tt_effect_pointer(e,1,200,170,14010);tt_effect_tick(e,14680);assert(e->mode==0);
 tt_effect_cancel(e);tap(e,170,50,15000);tap(e,170,50,15200);assert(e->mode==0);
 tap(e,170,50,15400);assert(e->mode==TT_EFFECT_CODE);
 tt_effect_pointer(e,0,180,180,15700);tt_effect_pointer(e,2,180,180,15750);tt_effect_tick(e,16030);assert(e->mode==0);
 tt_effect_cancel(e);tap(e,180,180,16100);tap(e,180,180,16300);tap(e,180,180,16500);
 assert(e->mode==TT_EFFECT_CODE&&!e->theme_advance);tt_effect_cancel(e);
 for(unsigned mode=0;mode<5;mode++)for(unsigned r=0;r<4;r++){
  tt_effect_cancel(e);tt_effect_clock(e,"23:59");
  if(mode==1){tap(e,180,180,17000);tap(e,180,180,17200);tt_effect_tick(e,17700);assert(e->theme_advance);e->theme_advance=false;}
  if(mode==2){tt_effect_pointer(e,0,180,170,17000);tt_effect_tick(e,17680);}
  if(mode==3){tap(e,170,50,17000);tap(e,170,50,17200);tap(e,170,50,17400);}
  if(mode==4){assert(tt_effect_shake(e,17000));assert(e->mode==TT_EFFECT_FLY&&!e->theme_advance);assert(!tt_effect_shake(e,17001));}
  for(unsigned t=17800;t<18000;t+=16){tt_effect_tick(e,t);tt_raster_begin(&c,image.pixels,r,r,true,true);tt_effect_draw(e,&c,t);guards();}
 }
 tt_effect_cancel(e);tap(e,180,82,18100);tap(e,180,82,18300);tap(e,180,82,18500);assert(e->mode==TT_EFFECT_CODE);
 tt_effect_cancel(e);tt_effect_pointer(e,0,180,180,18600);assert(!tt_effect_shake(e,18601));tt_effect_cancel(e);
 assert(tt_effect_shake(e,18700));tt_effect_tick(e,21700);assert(e->mode==0);assert(!tt_effect_shake(NULL,21701));
 for(unsigned h=0;h<24;h++)for(unsigned m=0;m<60;m++){
  char text[6];snprintf(text,6,"%02u:%02u",h,m);tt_effect_clock(e,text);assert(e->count>0&&e->count<=336);
  for(unsigned i=0;i<e->count;i++)for(unsigned corner=0;corner<4;corner++){
   int px=e->tiles[i].home_x+((corner&1)?TT_CLOCK_TILE+2:-3);
   int py=e->tiles[i].home_y+((corner&2)?TT_CLOCK_TILE+2:-3);
   int dx=2*px-359,dy=2*py-359;assert(dx*dx+dy*dy<=360*360);
  }
  guards();
 }
 tt_effect_clock(e,"--:--");assert(e->count>0);tt_effect_pointer(e,0,-100,500,20000);assert(e->x==0&&e->y==359);
 tt_effect_pointer(e,3,0,359,20010);assert(!e->held);tt_effect_cancel(e);tt_effect_tick(NULL,0);tt_effect_cancel(NULL);
 puts("PASS TimeTiles production effects: repulsion/settling, delayed double vs canvas/title triple, single theme signal, shake start/guards/exit, 1440 round-glass strings and 4 rotations/clipping canaries");
}
'''
(out/'check.c').write_text(code, encoding='utf-8')
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP),
           ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe = out/'check.exe'
cmd = [str(ZIG), 'cc', '-std=c11', '-O2',
       '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I'+str(src),
       str(src/'effects.c'), str(src/'raster.c'), str(out/'check.c'), '-o', str(exe)]
log_path = root.parent/'outputs/timetiles-gestures-20261015/effects-check.log'
log_path.parent.mkdir(parents=True, exist_ok=True)
with log_path.open('w', encoding='utf-8') as log:
    log.write('Actual production C; host checks only, no board or simulator acceptance.\n')
    for args in (cmd, [str(exe)]):
        result = subprocess.run(args, env=env, capture_output=True, text=True)
        log.write(result.stdout+result.stderr);log.flush()
        print(result.stdout+result.stderr, end='');result.check_returncode()
