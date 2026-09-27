"""Actual borrowed-cache functions: zero-copy, pinned lifetime and safe fallback."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/eaf-transition-check'); out.mkdir(parents=True, exist_ok=True)
source = (root/'ui/ui_transition_cache.c').read_text()


# Locate definitions, not earlier calls.
def definition(name):
    import re
    start = re.search(r'^(?:bool|void) '+name+r'\(', source, re.M).start()
    end = source.index('{', start)+1; depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]


code = r'''
#include <assert.h>
#include <stdio.h>
#include "ui_transition_compositor.h"
typedef struct {int id;} lv_obj_t;
typedef struct {struct {unsigned cf,stride,w,h;} header;const uint8_t *data;} lv_image_dsc_t;
typedef struct {lv_obj_t *real;bool suspended,pinned;unsigned state,used;lv_image_dsc_t buffer;} surface_t;
#define LV_COLOR_FORMAT_RGB565_SWAPPED 27
#define DIRTY 1
static surface_t surfaces[2],*direct_sources[4];
static unsigned direct_count,direct_hits,direct_misses,clock_id,stops;
static bool enabled=true,direct_enabled=true,planned,running;
static lv_obj_t *borrowed_object;static const lv_image_dsc_t *borrowed_image;
static ui_compositor_frame_t submitted;
static surface_t *find(lv_obj_t *o){for(int i=0;i<2;i++)if(surfaces[i].real==o)return &surfaces[i];return NULL;}
static bool valid_cache(surface_t *s){return s&&s->state==2;}
static int lv_obj_get_style_radius(lv_obj_t *o,int sel){return 0;}
static void trace(surface_t *s,const char *a,const char *b,unsigned t){}
static void ui_transition_cache_request(lv_obj_t *o){}
bool ui_transition_compositor_active(void){return running;}
void ui_transition_compositor_stop(void){running=false;stops++;}
bool ui_transition_compositor_begin(const ui_compositor_frame_t *f){assert(!running);running=true;submitted=*f;return true;}
void ui_transition_compositor_present(const ui_compositor_frame_t *f){assert(running);submitted=*f;}
''' + '\n'.join(definition(n) for n in ['ui_transition_cache_direct_end', 'ui_transition_cache_direct', 'ui_transition_cache_borrow']) + r'''
int main(void){
 lv_obj_t home={0},target={1};uint8_t pixels[2]={0x12,0x34},other[2]={0x56,0x78};
 lv_image_dsc_t image={.header={27,720,360,360},.data=pixels};
 surfaces[0]=(surface_t){.real=&home,.suspended=true};
 surfaces[1]=(surface_t){.real=&target,.state=2,.buffer={.header={27,720,360,360},.data=other}};
 lv_obj_t *objects[]={&home,&target};int xy[]={-70,0,290,0};
 assert(!ui_transition_cache_direct(objects,xy,2,0,0,360,360,0));
 ui_transition_cache_borrow(&home,&image);
 assert(ui_transition_cache_direct(objects,xy,2,0,0,360,360,0));
 assert(submitted.layers[0].pixels==pixels&&submitted.layers[1].pixels==other);
 assert(submitted.layers[0].stride==720&&submitted.layers[0].x==-70&&surfaces[0].pinned);
 ui_transition_cache_borrow(&home,&image);assert(running&&stops==0);
 ui_transition_cache_direct_end();assert(!running&&!surfaces[0].pinned&&borrowed_image==&image);
 assert(pixels[0]==0x12&&pixels[1]==0x34); /* immutable, never swapped/freed */
 assert(ui_transition_cache_direct(objects,xy,2,0,0,360,360,0)); /* reverse/return */
 ui_transition_cache_borrow(NULL,NULL);assert(!running&&borrowed_image==NULL&&stops==2);
 assert(!ui_transition_cache_direct(objects,xy,2,0,0,360,360,0));
 image.header.cf=18;ui_transition_cache_borrow(&home,&image);assert(borrowed_image==NULL); /* prototype CPU order rejected */
 puts("PASS: borrowed native source/pointer/stride, pin/end/return/resume and cache-miss fallback");
}
'''
(out/'check.c').write_text(code)
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp', ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG', '-I'+str(root/'ui'),
                str(out/'check.c'), '-o', str(out/'check.exe')], env=env, check=True)
subprocess.run([str(out/'check.exe')], check=True)
