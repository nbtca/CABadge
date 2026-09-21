#include "shake_detector.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    shake_detector_t d={0};
    const int16_t rest[]={0,0,1024},bump[]={900,0,1024},other[]={-900,0,1024};
    CHECK(!shake_detect(&d,true,0,rest));
    for(unsigned t=20;t<1000;t+=20)CHECK(!shake_detect(&d,true,t,rest));
    CHECK(!shake_detect(&d,true,1000,bump));
    CHECK(!shake_detect(&d,true,1020,rest)); /* One short bump must not wake. */
    CHECK(!shake_detect(&d,true,1500,rest));
    CHECK(!shake_detect(&d,true,1600,bump));
    CHECK(shake_detect(&d,true,1700,other));
    CHECK(!shake_detect(&d,false,1800,bump));
    CHECK(!shake_detect(&d,false,1900,other));
    CHECK(!shake_detect(&d,true,2000,rest));
    CHECK(!shake_detect(&d,true,2100,bump));
    CHECK(!shake_detect(&d,true,2600,other)); /* Expired first impulse. */
    CHECK(!shake_detect(&d,false,2620,rest));
    CHECK(!shake_detect(&d,true,2640,bump)); /* New sleep gets a fresh baseline. */
    puts("PASS: shake wakes only asleep; rest, single bump, impulse window, reset on wake");return 0;
}
