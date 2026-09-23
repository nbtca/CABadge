#include "miner.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
    miner_t g={0};
    for(int d=0;d<3;d++)for(int level=1;level<=20;level++){
        miner_start(&g,level,d,level+123);assert(g.count>0&&g.count<=32&&g.time==45);
        int sum=0;for(int i=0;i<g.count;i++){assert(g.items[i].rare>=0&&g.items[i].rare<47);sum+=g.items[i].value;}
        assert(g.target<=sum);
        miner_pause(&g);float t=g.time;miner_step(&g,1);assert(g.time==t);miner_pause(&g);
        for(int i=0;i<3000;i++){if(i%50==0)miner_action(&g);miner_step(&g,.02f);assert(isfinite(g.length)&&g.held<g.count);}
        assert(g.state==MINER_OVER||g.state==MINER_CLEAR);
    }
    miner_start(&g,1,0,123);g.items[0]=(miner_item_t){.type=MINER_RARE,.value=600,.rare=46,.caught=true};g.count=1;g.held=0;g.claw=2;g.length=55;
    miner_step(&g,.02f);assert(g.score==600&&(g.dex&(UINT64_C(1)<<46))&&g.save_pending);
    uint64_t dex=g.dex;miner_start(&g,2,1,12);assert(g.dex==dex&&g.best>=600);
    puts("miner: seeded levels, finite collisions, pause, timeout, rare collection and persistence state PASS");
}
