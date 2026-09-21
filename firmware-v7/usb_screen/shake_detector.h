#ifndef JX_SHAKE_DETECTOR_H
#define JX_SHAKE_DETECTOR_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
/* SC7A20 high-resolution +/-2 g samples, shifted right four bits.
 * Tune on the assembled badge: roughly 0.7 g axis-summed change, two impulses. */
#define SHAKE_DELTA_RAW 700
#define SHAKE_WINDOW_MS 400
typedef struct {bool baseline;uint8_t hits;int16_t previous[3];uint32_t last_hit;} shake_detector_t;
static inline bool shake_detect(shake_detector_t *d,bool asleep,uint32_t now,const int16_t xyz[3]){
    if(!asleep){*d=(shake_detector_t){0};return false;}
    int delta=0;
    for(int i=0;i<3;i++){delta+=abs((int)xyz[i]-d->previous[i]);d->previous[i]=xyz[i];}
    if(!d->baseline){d->baseline=true;return false;}
    if(d->hits&&now-d->last_hit>SHAKE_WINDOW_MS)d->hits=0;
    if(delta<SHAKE_DELTA_RAW||(d->hits&&now-d->last_hit<60))return false;
    d->last_hit=now;
    if(++d->hits<2)return false;
    *d=(shake_detector_t){0};return true;
}
#endif
