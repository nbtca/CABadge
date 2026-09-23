#ifndef BADGE_MINER_H
#define BADGE_MINER_H
#include <stdint.h>
#include <stdbool.h>
enum { MINER_SMALL,MINER_TINY,MINER_BIG,MINER_DIAMOND,MINER_ROCK,MINER_RARE };
enum { MINER_PLAY,MINER_PAUSED,MINER_CLEAR,MINER_OVER,MINER_REVEAL,MINER_ANGRY };
typedef struct {float x,y,home,r,speed;int type,value,rare,dir;bool caught,collected;} miner_item_t;
typedef struct {
    uint32_t rng;uint64_t dex;int best,level,difficulty,score,target,state,resume,misses;
    float time,clock,phase,angle,length,pivot,pivot_time,pause_time;
    int claw,held,count;bool save_pending;miner_item_t items[32];
} miner_t;
void miner_start(miner_t *g,int level,int difficulty,uint32_t seed);
void miner_step(miner_t *g,float seconds);
void miner_action(miner_t *g);
void miner_pause(miner_t *g);
void miner_tip(const miner_t *g,float *x,float *y);
#endif
