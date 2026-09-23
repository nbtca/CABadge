#ifndef CABADGE_BACKLIGHT_CURVE_H
#define CABADGE_BACKLIGHT_CURVE_H
#include <stdbool.h>
/* W180TE010I spec p11: 3.3V through 5.1 ohms. PCB R13 supplies LEDA.
 * 10-bit LEDC; squared UI mapping gives finer control at low brightness. */
static inline unsigned backlight_duty(int brightness,bool asleep){
    if(asleep||brightness<=0)return 0;
    if(brightness>100)brightness=100;
    return ((unsigned)brightness*brightness*1023u+5000u)/10000u;
}
#endif
