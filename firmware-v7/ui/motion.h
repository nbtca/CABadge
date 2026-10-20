#ifndef CABADGE_MOTION_H
#define CABADGE_MOTION_H
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "shake_detector.h"

/* mg, milliseconds. Tune on the assembled shell, not an unloaded PCB. */
/* SC7A20 click engine at 400 Hz, +/-2 g: XYZ double-click, 250 mg,
 * pulse <=80 ms, 80 ms dead time followed by a 400 ms second-click window.
 * Silan internal-function application note, section 7.3. FDS stays zero. */
#define MOTION_CTRL1 0x77
#define MOTION_CLICK_CTRL2 0xB4
#define MOTION_CLICK_CFG 0x2A
#define MOTION_CLICK_THS 16
#define MOTION_CLICK_LIMIT 32
#define MOTION_CLICK_LATENCY 32
#define MOTION_CLICK_WINDOW 160
#define MOTION_FACE_DOWN_MS 1200
#define MOTION_FACE_UP_MS 300
#define MOTION_COOLDOWN_MS 1500
enum { MOTION_SHAKE=1,MOTION_DOUBLE_TAP=2,MOTION_FACE_DOWN=4,MOTION_FACE_UP=8 };
typedef struct {
    bool valid,asleep,down,up,click_high,cooling;
    uint32_t at,down_ms,up_ms,cooldown_at;
    int16_t previous[3];
    float gravity[3];
    shake_detector_t shake;
} badge_motion_t;
static inline unsigned badge_motion_sample(badge_motion_t *m,bool asleep,uint32_t now,const int16_t xyz[3],uint8_t click){
    unsigned dt=now-m->at,events=0;
    if(!m->valid||dt>150){
        bool cooling=m->cooling;uint32_t cooldown=m->cooldown_at;
        *m=(badge_motion_t){.valid=true,.asleep=asleep,.at=now,.cooling=cooling,.cooldown_at=cooldown,.click_high=(click&0x60)==0x60};
        for(int i=0;i<3;i++){m->gravity[i]=xyz[i];m->previous[i]=xyz[i];}
        return 0;
    }
    m->at=now;
    if(m->asleep!=asleep){m->asleep=asleep;m->shake=(shake_detector_t){0};}
    if(m->cooling&&now-m->cooldown_at>=MOTION_COOLDOWN_MS)m->cooling=false;
    int delta=0;for(int i=0;i<3;i++){delta+=abs(xyz[i]-m->previous[i]);m->previous[i]=xyz[i];}
    float norm=(float)xyz[0]*xyz[0]+(float)xyz[1]*xyz[1]+(float)xyz[2]*xyz[2];
    bool quiet=delta<70&&norm>750.f*750&&norm<1250.f*1250;
    bool double_click=(click&0x60)==0x60; /* IA and DCLICK, ignore SCLICK. */
    if(double_click&&!m->click_high)events|=MOTION_DOUBLE_TAP;
    m->click_high=double_click;
    float alpha=dt/(120.f+dt);
    for(int i=0;i<3;i++)m->gravity[i]+=(xyz[i]-m->gravity[i])*alpha;
    bool flat_down=xyz[2]<-800&&abs(xyz[0])<250&&abs(xyz[1])<250;
    bool flat_up=xyz[2]>700;
    m->down_ms=flat_down&&quiet?m->down_ms+dt:0;
    m->up_ms=flat_up&&quiet?m->up_ms+dt:0;
    if(!m->down&&m->down_ms>=MOTION_FACE_DOWN_MS){m->down=true;m->up=false;events|=MOTION_FACE_DOWN;}
    if(!m->up&&m->up_ms>=MOTION_FACE_UP_MS){m->up=true;m->down=false;events|=MOTION_FACE_UP;}
    if(!flat_down&&xyz[2]>-500)m->down=false;
    if(!flat_up)m->up=false;
    if(shake_detect(&m->shake,true,now,xyz)&&!m->cooling&&!(events&MOTION_DOUBLE_TAP))events|=MOTION_SHAKE;
    if(events&(MOTION_SHAKE|MOTION_DOUBLE_TAP)){m->cooling=true;m->cooldown_at=now;}
    return events;
}

typedef struct {float x,y,vx,vy;unsigned score,held_ms;int goal;} gravity_ball_t;
static const int gravity_goals[4][2]={{80,0},{0,80},{-80,0},{0,-80}};
static inline void gravity_ball_step(gravity_ball_t *b,float gx,float gy,float dt){
    dt=fminf(fmaxf(dt,0),.04f);
    float drag=expf(-1.8f*dt);
    b->vx=(b->vx+fminf(1,fmaxf(-1,gx))*380*dt)*drag;
    b->vy=(b->vy+fminf(1,fmaxf(-1,gy))*380*dt)*drag;
    b->x+=b->vx*dt;b->y+=b->vy*dt;
    float r=sqrtf(b->x*b->x+b->y*b->y);
    if(r>95){
        float nx=b->x/r,ny=b->y/r,normal=b->vx*nx+b->vy*ny;
        b->x=nx*95;b->y=ny*95;
        if(normal>0){b->vx-=1.5f*normal*nx;b->vy-=1.5f*normal*ny;}
    }
    float dx=b->x-gravity_goals[b->goal][0],dy=b->y-gravity_goals[b->goal][1];
    if(dx*dx+dy*dy<18*18&&b->vx*b->vx+b->vy*b->vy<90*90)b->held_ms+=(unsigned)(dt*1000);
    else b->held_ms=0;
    if(b->held_ms>=400){b->score++;b->goal=(b->goal+1)%4;b->held_ms=0;}
}
#endif
