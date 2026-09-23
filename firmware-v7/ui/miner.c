/* Rules adapted from github.com/66970010-boop/frog-miner, index.html.
 * Physics stays in the original 800x600 coordinates; the UI scales to round LCD. */
#include "miner.h"
#include <math.h>
#include <string.h>
static float random_unit(miner_t *g){g->rng=g->rng*1664525u+1013904223u;return (g->rng>>8)/16777216.f;}
static float range(miner_t *g,float a,float b){return a+(b-a)*random_unit(g);}
static int minimum(int a,int b){return a<b?a:b;}
static int rare_value(int id){const int first[]={600,650,650,600,700,550,600,650,550};return id<9?first[id]:550+(id-9)*37%300+70;}
static void place(miner_t *g,int type,int number,int rare){
    const float radii[]={30,24,50,18,38,36};
    for(int n=0;n<number&&g->count<32;n++)for(int attempt=0;attempt<400;attempt++){
        float a=range(g,-.9975f,.9975f),r=radii[type];
        if(type==MINER_DIAMOND)a=(random_unit(g)<.5f?-1:1)*range(g,.651f,1.029f);
        float limit=fminf(446,(554-130)/fmaxf(.52f,cosf(a)));
        float len=type==MINER_DIAMOND?range(g,limit*.82f,limit*.98f):range(g,160,limit);
        float x=400+sinf(a)*len,y=130+cosf(a)*len;if(y<265||x<r||x>800-r)continue;
        bool ok=true;for(int i=0;i<g->count;i++)if(hypotf(g->items[i].x-x,g->items[i].y-y)<g->items[i].r+r+14)ok=false;
        if(!ok)continue;
        int value=type==MINER_SMALL?(int)roundf(range(g,80,120)/10)*10:type==MINER_TINY?60:
            type==MINER_BIG?(int)roundf(range(g,450,600)/10)*10:type==MINER_DIAMOND?250:type==MINER_RARE?rare_value(rare):0;
        if(type==MINER_BIG&&g->difficulty)value+=400;
        g->items[g->count++]=(miner_item_t){.x=x,.y=y,.home=x,.r=r,.speed=range(g,30,85),.dir=random_unit(g)<.5f?-1:1,.type=type,.value=value,.rare=rare};break;
    }
}
void miner_start(miner_t *g,int level,int difficulty,uint32_t seed){
    uint64_t dex=g->dex;int best=g->best;memset(g,0,sizeof(*g));g->dex=dex;g->best=best;
    g->rng=seed;g->level=level;g->difficulty=difficulty;g->time=45;g->length=54;g->held=-1;g->pivot=400;
    g->phase=range(g,0,6.2831853f);
    place(g,MINER_SMALL,2+minimum(level,6),0);place(g,MINER_TINY,level>=4?2:1,0);
    place(g,MINER_BIG,minimum(1+(level+1)/2,5),0);place(g,MINER_DIAMOND,level>=2?2:1,0);place(g,MINER_ROCK,2+(level*2+2)/3,0);
    int unseen[47],n=0;for(int i=0;i<47;i++)if(!(g->dex&(UINT64_C(1)<<i)))unseen[n++]=i;
    int rare=n?unseen[(int)(random_unit(g)*n)]:(int)(random_unit(g)*47);place(g,MINER_RARE,1,rare);
    int total=0;for(int i=0;i<g->count;i++)total+=g->items[i].value;
    g->target=(int)roundf(total*.62f/50)*50;if(g->target<500)g->target=500;
}
void miner_tip(const miner_t *g,float *x,float *y){*x=g->pivot+sinf(g->angle)*g->length;*y=130+cosf(g->angle)*g->length;}
static void miss(miner_t *g){if(++g->misses>=3){g->misses=0;g->state=MINER_ANGRY;g->pause_time=3;}}
void miner_action(miner_t *g){if(g->state!=MINER_PLAY)return;if(g->claw==0)g->claw=1;else if(g->claw==1){g->claw=2;miss(g);}}
void miner_pause(miner_t *g){if(g->state==MINER_PAUSED)g->state=g->resume;else {g->resume=g->state;g->state=MINER_PAUSED;}}
static void step(miner_t *g,float dt){
    if(g->state==MINER_PAUSED||g->state==MINER_CLEAR||g->state==MINER_OVER)return;
    if(g->state==MINER_REVEAL||g->state==MINER_ANGRY){g->pause_time-=dt;if(g->pause_time<=0)g->state=MINER_PLAY;return;}
    g->clock+=dt;g->time-=dt;if(g->time<=0){g->time=0;g->state=MINER_OVER;return;}
    if(g->difficulty==2){g->pivot_time+=dt*.6f;g->pivot=400+sinf(g->pivot_time)*80;}
    for(int i=0;i<g->count;i++){miner_item_t *it=&g->items[i];if(g->difficulty&&it->type==MINER_BIG&&!it->caught&&!it->collected){
        it->x+=it->dir*it->speed*dt;if(it->x>it->home+80||it->x>750)it->dir=-1;if(it->x<it->home-80||it->x<50)it->dir=1;
    }}
    if(g->claw==0){g->phase+=dt*3.14159265f;g->angle=1.05f*sinf(g->phase);g->length=54;}
    else if(g->claw==1){
        g->length+=330*dt;float x,y;miner_tip(g,&x,&y);
        if(g->length>=470){g->claw=2;miss(g);return;}
        int closest=-1;float distance=1e9f;
        for(int i=0;i<g->count;i++){miner_item_t *it=&g->items[i];float d=hypotf(x-it->x,y-it->y);if(!it->collected&&!it->caught&&d<it->r+12&&d<distance){closest=i;distance=d;}}
        if(closest>=0){g->held=closest;g->claw=2;miner_item_t *it=&g->items[closest];it->caught=true;
            if(it->type==MINER_RARE){g->state=MINER_REVEAL;g->pause_time=2.2f;}
            else if(it->type==MINER_ROCK)miss(g);else if(it->type!=MINER_BIG)g->misses=0;
        }
    }else {
        static const float speed[]={250,285,70,330,90,260};miner_item_t *it=g->held>=0?&g->items[g->held]:NULL;
        float v=it?speed[it->type]:420;if(it&&it->type==MINER_BIG&&fmodf(g->clock,.8f)>.32f)v*=.05f;
        g->length-=v*dt;if(it)miner_tip(g,&it->x,&it->y);
        if(g->length<=54){g->length=54;if(it){it->collected=true;g->score+=it->value;
            if(it->type==MINER_RARE){g->dex|=UINT64_C(1)<<it->rare;g->save_pending=true;}
            if(g->score>g->best){g->best=g->score;g->save_pending=true;}
            if(g->score>=g->target)g->state=MINER_CLEAR;
        }g->held=-1;g->claw=0;}
    }
}
void miner_step(miner_t *g,float seconds){if(!isfinite(seconds)||seconds<=0)return;while(seconds>0){float dt=fminf(seconds,.02f);step(g,dt);seconds-=dt;}}
