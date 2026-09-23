"""Production reparent/restore and layout lifecycle; no simulator or visual claim."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
s=(root/'ui/badge_ui.c').read_text()
helpers=s[s.index('static void connection_wrap'):s.index('static void connection_bitmap_end(void){')]
layout=s[s.index('static void layout(void){'):s.index('static void tick(lv_timer_t')]
a=s.index('if(detail_motion.moving&&to==detail_from){');a=s.index('\n',a)+1;b=s.index('        }else{',a);reverse=s[a:b]
pre=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
static void ui_transition_cache_direct_end(void){}
static bool direct_external;
static void ui_transition_cache_identify(void *o,unsigned id){(void)o;(void)id;}
static void ui_transition_cache_alias(void *a,void *b){(void)a;(void)b;}
static void ui_transition_cache_keep(void *a,bool b){(void)a;(void)b;}
static void transition_input(bool active,int owner){(void)owner;assert(!active);}
static bool direct_layout(void){return false;}
static void ui_transition_cache_suspend(void *o,bool a,bool b){(void)o;(void)a;(void)b;}
typedef struct obj {struct obj *parent,*children[30];int count,x,y,w,h;bool hidden;} lv_obj_t;
static lv_obj_t pool[50];static int used;
static void lv_obj_set_parent(lv_obj_t *o,lv_obj_t *p){
 if(o->parent){lv_obj_t *old=o->parent;int i=0;while(old->children[i]!=o)i++;for(;i<old->count-1;i++)old->children[i]=old->children[i+1];old->count--;}
 o->parent=p;p->children[p->count++]=o;
}
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,int c,int r){(void)c;(void)r;assert(used<50);lv_obj_t *o=&pool[used++];o->x=x;o->y=y;o->w=w;o->h=h;if(p)lv_obj_set_parent(o,p);return o;}
static int lv_obj_get_child_count(lv_obj_t *o){return o->count;}
static lv_obj_t *lv_obj_get_child(lv_obj_t *o,int i){return o->children[i];}
static int lv_obj_get_x(lv_obj_t *o){return o->x;}
static int lv_obj_get_y(lv_obj_t *o){return o->y;}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_obj_set_x(lv_obj_t *o,int x){o->x=x;}
static void lv_obj_set_y(lv_obj_t *o,int y){o->y=y;}
static void lv_obj_update_layout(lv_obj_t *o){(void)o;}
static void lv_obj_move_foreground(lv_obj_t *o){(void)o;}
static void shown(lv_obj_t *o,bool on){if(o)o->hidden=!on;}
static void detail_cache_end(void){}
static void connection_bitmap_end(void){}
static void ui_transition_cache_register(lv_obj_t *o,bool b){(void)o;(void)b;}
static void ui_transition_cache_forget(lv_obj_t *o){(void)o;}
static void ui_transition_cache_end(lv_obj_t *o){(void)o;}
static bool ui_transition_cache_begin(lv_obj_t *o){(void)o;return false;}
static void ui_transition_cache_position(lv_obj_t *o){(void)o;}
static struct {bool down,moved;int owner;} drag;
static struct {bool reduced_motion;} state,*s=&state;
static float clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
#define UI_BG 0
static lv_obj_t *root,*details[5],*home[2],*drawers[2],*detail_from,*detail_to,*detail_images[2],*drawer_content[2];
static lv_obj_t *connection_background,*connection_viewport,*connection_content[4];
static bool connection_local,detail_cached;
static int current,detail_direction;
static struct {float value,velocity,target;bool moving;} detail_motion,display_motion,drawer_motion;
"""
post=r"""
int main(void){
 root=box(0,0,0,360,360,0,0);for(int i=0;i<2;i++){home[i]=box(root,0,0,360,360,0,0);drawers[i]=box(root,0,0,360,360,0,0);}
 for(int i=0;i<4;i++)details[i+1]=box(root,0,0,360,360,0,0);
 lv_obj_t *children[4][3];int xs[]={85,124,61},ys[]={36,31,100};
 for(int i=0;i<4;i++)for(int j=0;j<3;j++)children[i][j]=box(details[i+1],xs[j],ys[j],44,44,0,0);
 for(int n=0;n<100;n++){
  current=3;connection_enter();assert(connection_local&&connection_viewport->w==250&&connection_viewport->h==272);
  for(int i=0;i<4;i++)for(int j=0;j<3;j++){assert(children[i][j]->parent==connection_content[i]);assert(children[i][j]->x==xs[j]-50);}
  detail_from=connection_content[0];detail_to=connection_content[1];shown(detail_to,true);detail_direction=1;detail_motion.value=.5f;detail_motion.moving=true;layout();
  assert(detail_from->x==-125&&detail_to->x==125&&connection_background->x==0);
  lv_obj_t *old_from=detail_from,*old_to=detail_to;int old_x=old_from->x,new_x=old_to->x;
  REVERSE_PRODUCTION
  layout();assert(old_from->x==old_x&&old_to->x==new_x);
  REVERSE_PRODUCTION
  detail_motion.value=1;detail_motion.moving=false;current=4;layout();assert(connection_content[0]->hidden&&!connection_content[1]->hidden&&detail_from==0&&detail_to==0);
  connection_leave();assert(!connection_local&&connection_background->hidden);
  for(int i=0;i<4;i++)for(int j=0;j<3;j++){assert(children[i][j]->parent==details[i+1]);assert(children[i][j]->x==xs[j]&&children[i][j]->y==ys[j]);}
 }
 assert(used==27);puts("100 production viewport reparent/restore cycles; fixed background; animation cleanup PASS");
}
"""
stage=Path('F:/CABadgeBuild/temp/connection-viewport');stage.mkdir(exist_ok=True)
(stage/'test.c').write_text(pre+helpers+layout+post.replace('REVERSE_PRODUCTION','{'+reverse+'}'))
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(stage/'test.c'),'-o',str(stage/'test.exe')],env=env,check=True)
subprocess.run([str(stage/'test.exe')],check=True)
