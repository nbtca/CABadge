"""Run production activation/invalidation/ownership logic without LCD claims."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
s=(root/'ui/ui_transition_cache.c').read_text()
pre=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct obj {bool hidden;struct obj *parent;} lv_obj_t;
typedef struct {void *data;size_t data_size;} lv_draw_buf_t;
#define LV_OBJ_FLAG_HIDDEN 1
#define UI_COMPOSITOR_LAYERS 4
static void ui_transition_cache_direct_end(void){}
static void lv_obj_remove_flag(lv_obj_t *o,int f){(void)f;o->hidden=false;}
static void lv_obj_add_flag(lv_obj_t *o,int f){(void)f;o->hidden=true;}
static bool lv_obj_has_flag(lv_obj_t *o,int f){(void)f;return o->hidden;}
static lv_obj_t *lv_obj_get_parent(lv_obj_t *o){return o->parent;}
"""
types=s[s.index('#define SURFACES'):s.index('#if CABADGE_TRANSITION_CACHE_DEBUG')]
helpers=s[s.index('static surface_t *find'):s.index('static void release')]
core=s[s.index('void ui_transition_cache_request'):s.index('void ui_transition_cache_poll')]
pre+=types+helpers+r"""
static void trace(surface_t *s,const char *a,const char *b,uint64_t t){(void)s;(void)a;(void)b;(void)t;}
static void release(surface_t *s){assert(!s->pinned);s->state=INVALID;s->buffer.data=NULL;}
"""
post=r"""
int main(void){
 lv_obj_t full={0},local={0},child={.parent=&local},cold={0};
 surface_t *a=&surfaces[0],*b=&surfaces[1],*c=&surfaces[2];
 *a=(surface_t){.real=&full,.page_id=11,.revision=1,.cache_revision=1,.state=READY,.hot=true};
 *b=(surface_t){.real=&local,.page_id=20,.revision=1,.cache_revision=1,.state=READY};
 *c=(surface_t){.real=&cold,.page_id=30,.revision=1,.cache_revision=1,.state=READY};
 a->buffer.data=&full;b->buffer.data=&local;c->buffer.data=&cold;
 ui_transition_cache_alias(&full,&local);
 assert(!ui_transition_cache_begin(&local));assert(!local.hidden&&ui_transition_cache_active());
 ui_transition_cache_end(&local);
 ui_transition_cache_show(&local,false);ui_transition_cache_show(&local,true);
 assert(valid_cache(a)&&valid_cache(b)&&a->revision==1&&b->revision==1);
 ui_transition_cache_invalidate_reason(&child,"TEXT_CHANGED");
 assert(a->revision==2&&b->revision==2&&!valid_cache(a)&&!valid_cache(b));
 a->state=b->state=READY;a->cache_revision=b->cache_revision=2;
 unsigned nav[]={20,30,11};ui_transition_cache_plan(nav,3);
 assert(b->priority==0&&c->priority==1&&a->priority==1&&b->wanted);
 c->priority=4;c->used=1;b->pinned=true;a->used=0;
 assert(victim(NULL,true)==c); // pinned DMA source and hot page survive cold eviction
 c->pinned=true;assert(victim(NULL,true)==a); // hot only when no cold source is available
 a->pinned=true;assert(!victim(NULL,true));
 a->pinned=b->pinned=c->pinned=false;
 ui_transition_cache_forget(&cold);assert(c->state==INVALID&&!c->buffer.data);
 uint8_t pixels[]={0x34,0x12,0xcd,0xab};uint8_t *native=pixels;uint32_t bytes=sizeof(pixels);
"""
start=s.index('    for(uint32_t i=0;i<bytes;i+=2)');end=s.index('\n',start)
post+=s[start:end]+r"""
 assert(pixels[0]==0x12&&pixels[1]==0x34&&pixels[2]==0xab&&pixels[3]==0xcd);
 puts("Native single-copy swap, real-widget fallback, visual aliases, hot/LRU and active-pin safety PASS");
}
"""
stage=Path('F:/CABadgeBuild/temp/transition-cache-check');stage.mkdir(exist_ok=True)
(stage/'check.c').write_text(pre+core+post)
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(stage/'check.c'),'-o',str(stage/'check.exe')],env=env,check=True)
subprocess.run([str(stage/'check.exe')],check=True)
