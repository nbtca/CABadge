"""Production battle rules and two independent GUI controllers over lossy queues.
This checks code/protocol behavior, not physical BLE, touch or display acceptance.
"""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
from pathlib import Path
import re
import subprocess

root = ROOT
out = (TEST_TEMP/'pokemon-test')
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP),
           ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
zig = ZIG


def run(name, code):
    source = out/f'{name}.c'
    source.write_text(code, encoding='utf-8')
    exe = out/f'{name}.exe'
    subprocess.run([str(zig), 'cc', '-O2', '-UNDEBUG', '-I'+str(root/'ui'),
                    str(source), '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)


run('rules', r'''
#include "pokemon_core.h"
#include <assert.h>
#include <stdio.h>
static void roundtrip(const pk_battle_t *b){
 uint8_t wire[20],again[20];pk_packet_t p={.op=PK_STATE,.state=*b},q;
 pk_encode(wire,&p);assert(pk_decode(wire,20,&q));pk_encode(again,&q);assert(!memcmp(wire,again,20));
}
int main(void){
 assert(pk_damage(&pk_species[0],&pk_species[2],&pk_species[0].moves[1],15,false)==50);
 assert(pk_damage(&pk_species[1],&pk_species[0],&pk_species[1].moves[1],15,false)==54);
 assert(pk_damage(&pk_species[2],&pk_species[1],&pk_species[2].moves[1],15,false)==56);
 assert(pk_effect(PK_ELECTRIC,PK_WATER)==8&&pk_effect(PK_GRASS,PK_POISON)==2);
 pk_battle_t b;pk_lobby(&b,0,0xABCDEF01);assert(b.session==0xEF01);roundtrip(&b);
 pk_packet_t p={.op=PK_SELECT,.session=b.session,.value=1};assert(pk_apply(&b,&p));
 pk_battle_t before=b;assert(!pk_apply(&b,&p)&&!memcmp(&b,&before,sizeof(b)));
 p.op=PK_READY;p.value=2;assert(!pk_apply(&b,&p));p.value=1;b.ready=1;assert(pk_apply(&b,&p)&&b.phase==PK_BATTLE);
 assert(!pk_choice(&b,2,0)&&!pk_choice(&b,0,5)&&!pk_choice(&b,0,PK_STRUGGLE));roundtrip(&b);
 assert(pk_submit(&b,0,1,0));before=b;assert(!pk_submit(&b,0,1,0)&&!memcmp(&b,&before,sizeof(b)));
 p=(pk_packet_t){.op=PK_ACTION,.session=b.session,.turn=0,.value=2};assert(pk_apply(&b,&p)&&b.turn==1&&!b.ready);
 before=b;assert(!pk_apply(&b,&p)&&!memcmp(&b,&before,sizeof(b)));p.session++;assert(!pk_apply(&b,&p));roundtrip(&b);
 pk_lobby(&b,3,1);b.species[1]=3;pk_start(&b);assert(pk_submit(&b,0,1,0));assert(pk_submit(&b,1,0,0)&&b.first==1);
 pk_lobby(&b,0,3);b.species[1]=1;pk_start(&b);memset(b.pp,0,sizeof(b.pp));
 assert(!pk_choice(&b,0,0)&&pk_choice(&b,0,4));pk_submit(&b,0,4,0);pk_submit(&b,1,4,0);assert(b.last[0]==4&&b.last[1]==4);roundtrip(&b);
 unsigned misses=0,crit=0;
 for(unsigned seed=1;seed<=64;seed++)for(unsigned a=0;a<4;a++)for(unsigned d=0;d<4;d++){
  pk_lobby(&b,a,seed);b.species[1]=d;pk_start(&b);pk_battle_t clone=b;
  while(b.phase==PK_BATTLE){
   unsigned turn=b.turn,m0=pk_cpu(&b,0),m1=pk_cpu(&b,1);assert(pk_submit(&b,0,m0,turn));assert(pk_submit(&b,1,m1,turn));
   assert(pk_submit(&clone,1,m1,turn)&&pk_submit(&clone,0,m0,turn));assert(!memcmp(&b,&clone,sizeof(b)));
   assert(b.hp[0]<=pk_species[a].hp&&b.hp[1]<=pk_species[d].hp&&b.turn<=100);roundtrip(&b);
   misses+=!!b.miss;crit+=!!b.critical;
  }
  assert(b.turn>0&&(!b.hp[0]||!b.hp[1]||b.turn==100));before=b;assert(!pk_submit(&b,0,0,b.turn)&&!memcmp(&b,&before,sizeof(b)));
 }
 assert(misses&&crit);
 uint8_t wire[21];p=(pk_packet_t){.op=PK_STATE,.state=b};pk_encode(wire,&p);pk_packet_t q;
 for(unsigned n=0;n<20;n++)assert(!pk_decode(wire,n,&q));assert(!pk_decode(wire,21,&q)&&!pk_decode(NULL,20,&q));
 for(unsigned i=0;i<20;i++){uint8_t old=wire[i];wire[i]=255;if(i<2||i==8||i==9||i==10||i==17)assert(!pk_decode(wire,20,&q));wire[i]=old;}
 p=(pk_packet_t){.op=PK_ACTION,.session=1,.value=3,.turn=4};pk_encode(wire,&p);assert(pk_decode(wire,20,&q)&&q.value==3&&q.turn==4);
 wire[8]=1;assert(!pk_decode(wire,20,&q));wire[8]=0;wire[2]=5;assert(!pk_decode(wire,20,&q));
 pk_lobby(&b,0,5);b.species[1]=1;pk_start(&b);
 uint32_t fuzz=1;for(unsigned k=0;k<100000;k++){for(unsigned i=0;i<20;i++){fuzz=fuzz*1664525+1013904223;wire[i]=fuzz>>24;}pk_decode(wire,20,&q);}
 puts("PASS production rules: fixed damage vectors, dual types, priority, PP/Struggle, same-seed determinism, 1024 complete battles, misses/criticals, stale/duplicate/wrong-session inputs, packed state and 100000 malformed packets");
}
''')

source = (root/'ui/pokemon.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'^\w[^\n]*\b'+name+r'\([^;\n]*\)\s*\{', source, re.M)
    assert match, name
    start = match.start()
    end = source.index('{', start)+1
    depth = 1
    while depth:
        depth += (source[end] == '{')-(source[end] == '}')
        end += 1
    return source[start:end]+'\n'


code = r'''
#include "pokemon_core.h"
#include "peer_link.h"
#include <assert.h>
#include <stdio.h>
typedef int lv_obj_t,lv_event_t;
enum {VIEW_MENU,VIEW_ROOM,VIEW_FIELD,VIEW_RESULT,LV_STATE_DISABLED=1,LV_OBJ_FLAG_HIDDEN=2};
typedef struct {pk_battle_t b;pk_packet_t p;unsigned select,mode,v,epoch,status,key;bool start,ok,waiting,anim;
 uint32_t at,seen,sent,action,animation;uint8_t hp[2];peer_link_info_t link;} gui_t;
static gui_t g[2];static unsigned who;static uint32_t now;static int objects[10];
static lv_obj_t *root=objects,*note=objects,*sprites[2]={objects+1,objects+2},*particles[3]={objects+3,objects+4,objects+5};
static void (*return_to_apps)(void);static char message[128];static const int type_colors[]={1,2,3,4,5,6};
#define battle g[who].b
#define pending g[who].p
#define selected g[who].select
#define net_mode g[who].mode
#define view g[who].v
#define link_epoch g[who].epoch
#define link_status g[who].status
#define room_key g[who].key
#define starting g[who].start
#define valid g[who].ok
#define has_pending g[who].waiting
#define animating g[who].anim
#define link_at g[who].at
#define seen_at g[who].seen
#define sent_at g[who].sent
#define action_at g[who].action
#define anim_at g[who].animation
#define before_hp g[who].hp
static uint8_t queue[2][64][20];static unsigned queued[2],stopped[2],shown_hp[2][2];static bool drop_states,drop_actions;
static uint32_t lv_tick_get(void){return now;}
static unsigned seed(void){return 0x12345678u+who;}
static void *lv_event_get_user_data(lv_event_t *e){return (void*)(intptr_t)*e;}
static void lv_obj_add_state(lv_obj_t *o,int f){}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){}
static void lv_obj_set_style_opa(lv_obj_t *o,int v,int sel){assert(v>=0&&v<=255);}
static void lv_label_set_text(lv_obj_t *o,const char *s){assert(strlen(s)<200);}
static void lv_obj_add_flag(lv_obj_t *o,int f){}
static void lv_obj_remove_flag(lv_obj_t *o,int f){}
static void lv_obj_set_style_bg_color(lv_obj_t *o,int c,int sel){}
static int lv_color_hex(int c){return c;}
static void controls(void){}
static void field(void){view=VIEW_FIELD;}
static void room(void){view=VIEW_ROOM;}
static void result(void){view=VIEW_RESULT;}
static void stop_link(void),menu(void);
static void bar(unsigned side,unsigned hp){shown_hp[who][side]=hp;}
static bool peer_link_mode(unsigned mode){g[who].link=(peer_link_info_t){.mode=mode,.status=mode==PEER_HOST?PEER_WAITING:mode==PEER_JOIN?PEER_SCANNING:PEER_OFF,.epoch=g[who].link.epoch+1};if(!mode)stopped[who]++;return true;}
static bool peer_link_connect(unsigned index){return index<3;}
static void peer_link_snapshot(peer_link_info_t *out){*out=g[who].link;}
static bool peer_link_send(const uint8_t wire[20]){
 pk_packet_t p;assert(pk_decode(wire,20,&p));assert((who==0)==(p.op==PK_STATE));
 if((drop_states&&p.op==PK_STATE)||(drop_actions&&p.op==PK_ACTION))return true;
 unsigned other=who^1;assert(queued[other]<64);memcpy(queue[other][queued[other]++],wire,20);return true;
}
static bool peer_link_receive(uint8_t wire[20]){if(!queued[who])return false;memcpy(wire,queue[who][0],20);memmove(queue[who],queue[who]+1,--queued[who]*20);return true;}
'''
# The radio boundary is mocked; the actual GUI input, network and animation functions run.
code = code.replace('static bool peer_link_', 'bool peer_link_').replace('static void peer_link_snapshot', 'void peer_link_snapshot')
for name in ('me', 'stop_link', 'send_state', 'request', 'animate_from', 'disconnected', 'input', 'animate', 'network', 'pokemon_poll'):
    code += function(name)
code += r'''
static void menu(void){stop_link();valid=animating=false;view=VIEW_MENU;}
static void click(unsigned side,int id){who=side;lv_event_t event=id;input(&event);}
static void frame(unsigned side){who=side;pokemon_poll(true);}
static void settle(unsigned ms){now+=ms;frame(0);frame(1);frame(0);frame(1);}
static void equal_state(void){uint8_t a[20],b[20];pk_packet_t p={.op=PK_STATE,.state=g[0].b};pk_encode(a,&p);p.state=g[1].b;pk_encode(b,&p);assert(!memcmp(a,b,20));}
int main(void){
 now=100;click(0,11);click(0,2);assert(g[0].mode==PEER_HOST);now+=100000;frame(0);assert(!stopped[0]);
 click(1,13);click(1,3);g[0].link.status=g[1].link.status=PEER_READY;settle(10);assert(g[0].b.species[1]==3&&g[1].ok);
 click(0,4);click(1,4);settle(600);assert(g[0].b.phase==PK_BATTLE&&g[1].b.phase==PK_BATTLE);equal_state();
 drop_actions=true;click(1,32);settle(100);assert(g[1].waiting&&!(g[0].b.ready&2));drop_actions=false;
 settle(600);assert(!g[1].waiting&&(g[1].b.ready&2));
 drop_states=true;click(0,32);frame(0);assert(g[0].b.turn==1&&g[1].b.turn==0);drop_states=false;
 settle(1100);assert(g[1].b.turn==1);equal_state();settle(2200);assert(!g[0].anim&&!g[1].anim);
 assert(shown_hp[1][0]==g[1].b.hp[0]&&shown_hp[1][1]==g[1].b.hp[1]);
 pk_packet_t stale={.op=PK_ACTION,.session=g[0].b.session,.turn=0,.value=2};pk_encode(queue[0][queued[0]++],&stale);
 unsigned revision=g[0].b.revision;frame(0);assert(g[0].b.revision==revision);
 while(g[0].b.phase==PK_BATTLE){
  unsigned m0=pk_cpu(&g[0].b,0),m1=pk_cpu(&g[1].b,1);click(0,30+(m0==4?0:m0));click(1,30+(m1==4?0:m1));settle(600);settle(2400);equal_state();
 }
 assert(g[0].v==VIEW_RESULT&&g[1].v==VIEW_RESULT);
 click(0,5);assert(stopped[0]&&g[0].v==VIEW_MENU);who=1;g[1].link.status=PEER_ERROR;frame(1);assert(stopped[1]&&g[1].v==VIEW_MENU);
 queued[0]=queued[1]=0;click(0,2);g[0].link.status=PEER_READY;frame(0);now+=15001;frame(0);assert(g[0].v==VIEW_MENU);
 click(1,3);who=1;pokemon_poll(false);assert(g[1].v==VIEW_MENU&&!g[1].mode);
 for(unsigned s=0;s<4;s++){
  click(0,10+s);click(0,1);while(g[0].b.phase==PK_BATTLE){unsigned m=pk_cpu(&g[0].b,0);click(0,30+(m==4?0:m));now+=2500;frame(0);}assert(g[0].v==VIEW_RESULT);
 }
 puts("PASS production two-GUI controllers: character selection, long lobby wait, readiness, action retry, lost-state recovery, duplicate rejection, shared HP/PP/result, actual animation completion, all four CPU matches, disconnect and sleep cleanup");
}
'''
run('controllers', code)
