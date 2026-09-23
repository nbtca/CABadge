"""Exercise the production menu constructor and click routing, without a simulator."""
from pathlib import Path
import os,re,subprocess

root=Path(__file__).resolve().parent
source=(root/'ui/apps.c').read_text('utf-8')
constructor=re.search(r'static void show_list\(void\)\{.*?\n\}',source,re.S).group()
choose=re.search(r'static void choose\(lv_event_t \*e\)\{[^\n]+',source).group()
unit=Path('F:/CABadgeBuild/temp/apps-menu-test.c')
unit.write_text(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef struct {void *user;} lv_event_t;
static void *panel;
enum {APP_LIST};
static int count,selected=-1,ids[4],positions[4][4];
static const char *names[4];
static void (*callbacks[4])(lv_event_t*);
static void *lv_event_get_user_data(lv_event_t *e){return e->user;}
static void show_map(void){selected=0;}
static void show_menu(void){selected=1;}
static void show_grok(void){selected=2;}
static void clear_panel(int mode){assert(mode==APP_LIST);count=0;}
static void heading(const char *text){(void)text;}
static void button(void *p,const char *text,int x,int y,int w,int h,void (*cb)(lv_event_t*),int id){
    (void)p;assert(count<4);names[count]=text;ids[count]=id;callbacks[count]=cb;
    positions[count][0]=x;positions[count][1]=y;positions[count][2]=w;positions[count][3]=h;count++;
}
''' + choose + '\n' + constructor + r'''
int main(void){
    show_list();assert(count==3);assert(!strcmp(names[2],"Grok"));
    for(int i=0;i<count;i++){
        assert(positions[i][0]>=0&&positions[i][0]+positions[i][2]<=360);
        assert(positions[i][1]>=0&&positions[i][1]+positions[i][3]<=360);
        if(i)assert(positions[i-1][1]+positions[i-1][3]<=positions[i][1]);
        lv_event_t e={(void*)(intptr_t)ids[i]};callbacks[i](&e);assert(selected==i);
    }
    puts("PASS: production menu creates three non-overlapping entries; Grok button routes to Grok");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
exe=unit.with_suffix('.exe')
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True)
