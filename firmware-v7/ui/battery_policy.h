#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Voltage bands, not a calibrated state-of-charge percentage. */
typedef struct {
    int filtered_mv,level;
    bool latched,pending,low_tracking,recovery_tracking;
    uint32_t low_since,recovery_since;
} badge_battery_t;

static inline void badge_battery_sample(badge_battery_t *b,int mv,uint32_t now){
    if(mv<1000||mv>4500){
        b->filtered_mv=0;b->level=-1;b->pending=false;
        b->low_tracking=b->recovery_tracking=false;return;
    }
    b->filtered_mv=b->filtered_mv?(b->filtered_mv*3+mv+2)/4:mv;
    int v=b->filtered_mv;
    b->level=v<=3500?0:v<=3650?1:v<=3800?2:v<=4000?3:4;
    /* Use consecutive actual samples for the warning, not a lagging filter. */
    if(mv<=3500){
        b->recovery_tracking=false;
        if(!b->low_tracking){b->low_tracking=true;b->low_since=now;}
        if(!b->latched&&(uint32_t)(now-b->low_since)>=10000){b->latched=true;b->pending=true;}
    }else{
        b->low_tracking=false;
        if(mv>=3700){
            if(!b->recovery_tracking){b->recovery_tracking=true;b->recovery_since=now;}
            if((uint32_t)(now-b->recovery_since)>=30000){b->latched=false;b->pending=false;}
        }else b->recovery_tracking=false;
    }
}
