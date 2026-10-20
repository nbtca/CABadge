#include "effects.h"
#include "timetiles_assets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash(uint32_t n)
{
    n=(n^(n>>16))*UINT32_C(0x7feb352d);
    n=(n^(n>>15))*UINT32_C(0x846ca68b);
    return n^(n>>16);
}
static void add(tt_effect_t *e,int x,int y,unsigned color)
{
    if(e->count>=336)return;
    e->tiles[e->count++]=(tt_tile_t){.x=x*256,.y=y*256,.home_x=x,.home_y=y,.color=color};
}
void tt_effect_clock(tt_effect_t *e,const char *text)
{
    if(!e||!text||strlen(text)!=5||text[2]!=':'||e->mode||!strcmp(e->text,text))return;
    memcpy(e->text,text,6);e->count=0;
    int x=TT_CLOCK_X;
    for(unsigned i=0;i<5;i++){
        if(i==2){add(e,x,TT_CLOCK_Y+3*TT_CLOCK_ROW,0);add(e,x,TT_CLOCK_Y+7*TT_CLOCK_ROW,1);x+=2*TT_CLOCK_COLUMN;continue;}
        unsigned id=text[i]>='0'&&text[i]<='9'?(unsigned)(text[i]-'0'):10;
        for(unsigned row=0;row<10;row++)for(unsigned col=0;col<6;col++)
            if(tt_digit_font[id][row]&(1u<<(5-col)))add(e,x+col*TT_CLOCK_COLUMN,TT_CLOCK_Y+row*TT_CLOCK_ROW,(i+row+col/2)%3);
        x+=7*TT_CLOCK_COLUMN;
    }
}
void tt_effect_cancel(tt_effect_t *e)
{
    if(!e)return;
    e->held=e->moved=e->long_press=e->active=false;e->mode=TT_EFFECT_CLOCK;e->radius=0;
    e->taps=0;e->tap_at=e->release_at=e->last_step=0;
    for(unsigned i=0;i<e->count;i++){
        tt_tile_t *t=&e->tiles[i];t->x=t->home_x*256;t->y=t->home_y*256;t->vx=t->vy=0;
    }
}
static void start(tt_effect_t *e,unsigned mode,uint64_t now)
{
    e->mode=mode;e->started=now;e->seed=hash((uint32_t)now^UINT32_C(0x434f4445));
    e->active=true;e->release_at=0;e->taps=0;e->last_step=now;
    if(mode==TT_EFFECT_NOVA)e->theme_advance=true;
    if(mode==TT_EFFECT_NOVA||mode==TT_EFFECT_FLY)for(unsigned i=0;i<e->count;i++){
        tt_tile_t *t=&e->tiles[i];uint32_t h=hash(e->seed+i*953);
        float a=(h&255)*6.2831853f/256.f;
        int radius=79+(h>>8)%218;
        t->vx=(int)(cosf(a)*radius*256-t->home_x*256+180*256)/24;
        t->vy=(int)(sinf(a)*radius*256-t->home_y*256+169*256)/24;
    }
}
bool tt_effect_shake(tt_effect_t *e,uint64_t now)
{
    if(!e||e->held||e->mode)return false;
    start(e,TT_EFFECT_FLY,now);e->last_step=now;return true;
}
void tt_effect_pointer(tt_effect_t *e,unsigned event,int x,int y,uint64_t now)
{
    if(!e||event>TT_TOUCH_CANCEL)return;
    x=x<0?0:x>359?359:x;y=y<0?0:y>359?359:y;
    e->x=x;e->y=y;
    if(event==TT_TOUCH_CANCEL){
        e->held=false;e->taps=0;e->tap_at=0;
        if(e->mode==TT_EFFECT_MOSAIC)e->release_at=now;
        return;
    }
    if(e->mode){
        if(event==TT_TOUCH_DOWN)e->held=true;
        if(event==TT_TOUCH_UP){e->held=false;e->release_at=now;}
        return;
    }
    if(event==TT_TOUCH_DOWN){
        e->held=true;e->moved=e->long_press=false;e->origin_x=x;e->origin_y=y;e->down_at=now;
        e->active=true;e->last_step=now;return;
    }
    if(!e->held)return;
    int dx=x-e->origin_x,dy=y-e->origin_y;
    if(dx*dx+dy*dy>18*18)e->moved=true;
    if(event!=TT_TOUCH_UP)return;
    e->held=false;
    if(e->moved||e->long_press||now-e->down_at>=420){e->taps=0;e->tap_at=0;return;}
    bool title=x>=TT_TITLE_LEFT&&x<TT_TITLE_RIGHT&&y>=TT_TITLE_TOP&&y<TT_TITLE_BOTTOM;
    if(!title&&(y<TT_TITLE_BOTTOM||y>=256)){e->taps=0;e->tap_at=0;return;}
    dx=x-e->tap_x;dy=y-e->tap_y;
    if(!e->taps||title!=e->tap_title||now-e->tap_at>=TT_TAP_GAP||dx*dx+dy*dy>=1702)e->taps=0;
    e->taps++;e->tap_at=now;e->tap_x=x;e->tap_y=y;e->tap_title=title;
    if(e->taps>=3)start(e,TT_EFFECT_CODE,now);
}
static void step(tt_effect_t *e,uint64_t now)
{
    // Recovered 0x50021a70: 16ms steps, radius 80/256, spring 15/256, damping 184/256.
    e->radius+=( (e->held&&e->mode==0&&e->origin_y>=TT_TITLE_BOTTOM&&e->origin_y<256?84*256:0)-e->radius)*80/256;
    if(e->radius<16)e->radius=0;
    e->active=e->held||e->radius!=0||e->mode!=0;
    for(unsigned i=0;i<e->count;i++){
        tt_tile_t *t=&e->tiles[i];int tx=t->home_x,ty=t->home_y;
        if(e->mode==TT_EFFECT_NOVA&&now-e->started<384){t->x+=t->vx;t->y+=t->vy;continue;}
        if(e->mode==TT_EFFECT_FLY&&now-e->started<1600){
            t->x+=t->vx;t->y+=t->vy;
            float dx=t->x/256.f+TT_CLOCK_TILE/2.f-180,dy=t->y/256.f+TT_CLOCK_TILE/2.f-180;
            float r=sqrtf(dx*dx+dy*dy);
            if(r>165){
                float nx=dx/r,ny=dy/r,v=t->vx*nx+t->vy*ny;
                t->x=(int)((180+nx*165-TT_CLOCK_TILE/2.f)*256);
                t->y=(int)((180+ny*165-TT_CLOCK_TILE/2.f)*256);
                if(v>0){t->vx-=(int)(2*v*nx);t->vy-=(int)(2*v*ny);}
            }
            continue;
        }
        if(e->radius&&e->mode==0){
            float dx=tx+TT_CLOCK_TILE/2-e->x,dy=ty+TT_CLOCK_TILE/2-e->y,d=sqrtf(dx*dx+dy*dy);
            float radius=e->radius/256.f;
            if(d<radius+19.5f){
                if(d<1){dx=i&1?1:-1;dy=0;d=1;}
                float push=d<radius?radius-d+3:(radius+19.5f-d)*4/26;
                tx+=(int)(dx*push/d);ty+=(int)(dy*push/d);
            }
        }
        t->vx=(t->vx+(tx*256-t->x)*15/256)*184/256;
        t->vy=(t->vy+(ty*256-t->y)*15/256)*184/256;
        t->x+=t->vx;t->y+=t->vy;
        if(!e->held&&!e->radius&&!e->mode&&abs(t->x-tx*256)<64&&abs(t->y-ty*256)<64&&abs(t->vx)<16&&abs(t->vy)<16){
            t->x=tx*256;t->y=ty*256;t->vx=t->vy=0;
        }
        if(t->x!=t->home_x*256||t->y!=t->home_y*256||t->vx||t->vy)e->active=true;
    }
}
bool tt_effect_tick(tt_effect_t *e,uint64_t now)
{
    if(!e)return false;
    if(!e->held&&!e->mode&&e->taps&&now-e->tap_at>=TT_TAP_GAP){
        if(e->taps==2&&!e->tap_title)start(e,TT_EFFECT_NOVA,now);
        else {e->taps=0;e->tap_at=0;}
    }
    if(e->held&&!e->mode&&!e->moved&&e->origin_y>=TT_TITLE_BOTTOM&&e->origin_y<256&&now-e->down_at>=680){
        e->long_press=true;start(e,TT_EFFECT_MOSAIC,now);
    }
    if(e->mode&&((e->release_at&&now-e->release_at>=(e->mode==TT_EFFECT_MOSAIC?680u:280u))||
        (e->mode!=TT_EFFECT_MOSAIC&&now-e->started>=(e->mode==TT_EFFECT_NOVA?2180u:3000u)))){
        tt_effect_cancel(e);return true;
    }
    if(!e->active)return false;
    uint64_t delta=now-e->last_step;
    if(delta>64){e->last_step=now-64;delta=64;}
    while(delta>=16){e->last_step+=16;step(e,e->last_step);delta-=16;}
    return true;
}
void tt_effect_draw(const tt_effect_t *e,tt_canvas_t *c,uint64_t now)
{
    if(!e)return;
    if(e->mode==TT_EFFECT_CODE){
        uint64_t elapsed=now-e->started;
        static const uint8_t zero[][2]={{1,0},{0,1},{2,1},{0,3},{2,3},{1,4}};
        static const uint8_t one[][2]={{1,0},{1,1},{1,2},{1,3},{1,4}};
        for(unsigned col=0;col<11;col++){
            uint32_t h=hash(e->seed+col*UINT32_C(0x9e3779b9));
            int head=(int)((h%480+elapsed*(74+h%71)/1000)%500)*3/4;
            for(unsigned row=0;row<10;row++){
                int y=head-(int)row*32;
                bool bit=hash(e->seed+col*131+row*977)&1;
                const uint8_t (*glyph)[2]=bit?one:zero;
                if(y>=0&&y<360)for(unsigned p=0;p<(bit?5u:6u);p++)
                    tt_rect(c,9+col*32+glyph[p][0]*3,y+glyph[p][1]*3,3,3,c->color[row?1+row%3:0]);
            }
        }
        return;
    }
    float strength=1.f;
    if(e->mode==TT_EFFECT_MOSAIC){
        strength=fminf(1.f,(now-e->started)/680.f);strength=strength*strength*(3-2*strength);
        if(e->release_at){float exit=fminf(1.f,(now-e->release_at)/680.f);strength*=1-exit*exit*(3-2*exit);}
    }
    for(unsigned i=0;i<e->count;i++){
        const tt_tile_t *t=&e->tiles[i];int x=t->x/256,y=t->y/256;
        if(e->mode==TT_EFFECT_MOSAIC){
            float dx=e->x-(x+TT_CLOCK_TILE/2),dy=e->y-(y+TT_CLOCK_TILE/2),d=sqrtf(dx*dx+dy*dy);
            if(d<1)d=1;
            float pull=(d/41.25f+2.25f)*strength;
            x+=(int)(dx*pull/d);y+=(int)(dy*pull/d);
        }
        uint16_t color=c->color[(unsigned[]){1,3,4}[t->color]];
        tt_tile(c,x,y,TT_CLOCK_TILE,color);
    }
    if(e->mode==TT_EFFECT_FLY)tt_small(c,"TIME TILES FLY",102,48,2,c->color[0]);
}
