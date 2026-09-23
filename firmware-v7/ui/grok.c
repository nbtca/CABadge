/* Native Grok Ball adaptation. Geometry: tycoding, MIT (grok_source/LICENSE). */
#include "grok.h"
#include "src/misc/cache/lv_cache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#define SIDE 360
#define FACE_SCALE (360.f/256.f)
typedef struct {const char *name;uint8_t pool[8],count;uint16_t transition,blink_min,blink_max;uint32_t color;} emotion_t;
#include "grok_data.h"
static lv_obj_t *image,*controls,*name_label;
static uint16_t *base,*pixels;
static lv_image_dsc_t picture;
static void (*go_back)(void);
static float current[2][48][2],start[2][48][2],target[2][48][2];
static float gaze_x,gaze_y,want_x,want_y;
static uint32_t changed,next_shape,next_blink,blink_at,last_tick,activity,seed=0x912381;
static int emotion,pool_index,theme;
static bool held,moved,long_press,full_redraw;
static lv_area_t previous_eyes;
static lv_point_t press;
static float clamp(float v,float a,float b){return fminf(b,fmaxf(a,v));}
static uint32_t random_between(uint32_t a,uint32_t b){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return a+(b>a?seed%(b-a+1):0);}
static uint16_t rgb(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
static uint16_t blend(uint16_t a,uint16_t b,float t){return rgb((int)(((a>>11)*8)*(1-t)+((b>>11)*8)*t),(int)((((a>>5)&63)*4)*(1-t)+(((b>>5)&63)*4)*t),(int)(((a&31)*8)*(1-t)+((b&31)*8)*t));}
static void background(void){
    full_redraw=true;
    uint32_t color=theme?0x292D35:emotions[emotion].color;
    int r=color>>16,g=(color>>8)&255,b=color&255;
    static const int dither[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(int y=0;y<SIDE;y++)for(int x=0;x<SIDE;x++){
        float dx=(x-179.5f)/180,dy=(y-179.5f)/180,rr=dx*dx+dy*dy;
        uint16_t c=rgb(16,24,32);
        if(rr<1.01f){float shade=clamp(.88f-.12f*dx-.16f*dy+.13f*sqrtf(fmaxf(0,1-rr)),.55f,1.08f);
            float noise=(dither[y&3][x&3]-7.5f)*.45f;
            uint16_t body=rgb((int)clamp(r*shade+noise,0,255),(int)clamp(g*shade+noise*.5f,0,255),(int)clamp(b*shade+noise,0,255));
            c=blend(c,body,clamp((1-sqrtf(rr))*180,0,1));}
        base[y*SIDE+x]=c;
    }
}
#ifndef GROK_RENDER_TEST
static void shape(int index,uint32_t now){
    memcpy(start,current,sizeof(start));
    for(int e=0;e<2;e++)for(int i=0;i<48;i++)for(int axis=0;axis<2;axis++)target[e][i][axis]=rings[index][e][i][axis]*.01f;
    changed=now;
}
static void select_emotion(int id,uint32_t now){
    emotion=(id+32)%32;pool_index=0;activity=now;
    shape(emotions[emotion].pool[0],now);next_shape=now+random_between(3000,6000);
    next_blink=now+random_between(emotions[emotion].blink_min,emotions[emotion].blink_max);blink_at=0;
    background();lv_label_set_text(name_label,emotions[emotion].name);lv_obj_set_style_text_color(name_label,theme?lv_color_white():lv_color_hex(0x191919),0);
}
 #endif
/* Even/odd scan conversion supports concave eye contours. Two subrows and
 * fractional edge coverage avoid a large supersampled framebuffer. */
static void eye(float pts[48][2],uint16_t ink){
    float lo=SIDE,hi=0;
    for(int i=0;i<48;i++){lo=fminf(lo,pts[i][1]);hi=fmaxf(hi,pts[i][1]);}
    for(int y=(int)clamp(floorf(lo),0,SIDE-1);y<=(int)clamp(ceilf(hi),0,SIDE-1);y++){
        float coverage[SIDE]={0};
        for(int sub=0;sub<2;sub++){
            float yy=y+.25f+.5f*sub,cross[48];int n=0;
            for(int i=0,j=47;i<48;j=i++)if((pts[i][1]>yy)!=(pts[j][1]>yy)){
                float xx=pts[i][0]+(yy-pts[i][1])*(pts[j][0]-pts[i][0])/(pts[j][1]-pts[i][1]);
                int k=n++;while(k>0&&cross[k-1]>xx){cross[k]=cross[k-1];k--;}cross[k]=xx;
            }
            for(int i=0;i+1<n;i+=2){float a=clamp(cross[i],0,SIDE),b=clamp(cross[i+1],0,SIDE);
                for(int x=(int)a;x<(int)ceilf(b);x++)coverage[x]+=.5f*fmaxf(0,fminf(x+1,b)-fmaxf(x,a));}
        }
        for(int x=0;x<SIDE;x++)if(coverage[x]>0)pixels[y*SIDE+x]=blend(pixels[y*SIDE+x],ink,clamp(coverage[x],0,1));
    }
}
#ifndef GROK_RENDER_TEST
void grok_tick(uint32_t now){
    if(!image||!pixels)return;
    float dt=clamp((now-last_tick)*.001f,0,.05f);last_tick=now;
    if(!held&&now-activity>60000&&emotion!=0)select_emotion(0,now);
    const emotion_t *def=&emotions[emotion];
    if(!held&&(int32_t)(now-next_shape)>=0){pool_index=(pool_index+1)%def->count;shape(def->pool[pool_index],now);next_shape=now+random_between(3000,6000);}
    if(def->blink_min&&(int32_t)(now-next_blink)>=0){blink_at=now;next_blink=now+random_between(def->blink_min,def->blink_max);}
    float open=1;
    if(blink_at&&now-blink_at<180)open=fmaxf(.04f,fabsf((now-blink_at)/90.f-1));
    if(emotion==0||emotion==6)open=.06f;
    if(!held){want_x=emotion==0?0:7*sinf(now*.0006f);want_y=emotion==0?0:3*sinf(now*.00043f);}
    float follow=1-expf(-12*dt);gaze_x+=(want_x-gaze_x)*follow;gaze_y+=(want_y-gaze_y)*follow;
    float t=clamp((now-changed)/(float)(def->transition?def->transition:500),0,1);t=t*t*(3-2*t);
    memcpy(pixels,base,SIDE*SIDE*2);
    lv_area_t changed_area={SIDE,SIDE,0,0};
    for(int e=0;e<2;e++){
        float points[48][2],cx=0,cy=0;
        for(int i=0;i<48;i++){for(int a=0;a<2;a++)current[e][i][a]=start[e][i][a]+(target[e][i][a]-start[e][i][a])*t;cx+=current[e][i][0]/48;cy+=current[e][i][1]/48;}
        float ey=clamp(14+cy+gaze_y,45,211),dy=(ey-128)/120;
        float hw=120*sqrtf(fmaxf(.1f,1-dy*dy)),theta=clamp((cx-114+gaze_x)/hw,-1.15f,1.15f);
        float ex=128+hw*sinf(theta)*.985f,sx=cosf(theta);
        for(int i=0;i<48;i++){points[i][0]=(ex+(current[e][i][0]-cx)*sx)*FACE_SCALE;points[i][1]=(ey+(current[e][i][1]-cy)*open+1.5f*sinf(now*.0015f))*FACE_SCALE;
            changed_area.x1=(int)fminf(changed_area.x1,floorf(points[i][0])-2);changed_area.x2=(int)fmaxf(changed_area.x2,ceilf(points[i][0])+2);
            changed_area.y1=(int)fminf(changed_area.y1,floorf(points[i][1])-2);changed_area.y2=(int)fmaxf(changed_area.y2,ceilf(points[i][1])+2);}
        eye(points,theme?rgb(244,243,240):rgb(25,25,25));
    }
    if(full_redraw){lv_obj_invalidate(image);full_redraw=false;}
    else {lv_area_t dirty={
        (int)fminf(previous_eyes.x1,changed_area.x1),(int)fminf(previous_eyes.y1,changed_area.y1),
        (int)fmaxf(previous_eyes.x2,changed_area.x2),(int)fmaxf(previous_eyes.y2,changed_area.y2)};
        lv_obj_invalidate_area(image,&dirty);}
    previous_eyes=changed_area;
}
static void touch(lv_event_t *event){
    lv_event_code_t code=lv_event_get_code(event);lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_point_t p;lv_indev_get_point(in,&p);uint32_t now=lv_tick_get();
    if(code==LV_EVENT_PRESSED){held=true;moved=false;long_press=false;press=p;activity=now;if(emotion==0)select_emotion(1,now);}
    if(code==LV_EVENT_PRESSING){if(abs(p.x-press.x)+abs(p.y-press.y)>8)moved=true;want_x=clamp((p.x-180)*.18f,-18,18);want_y=clamp((p.y-180)*.15f,-14,14);activity=now;}
    if(code==LV_EVENT_LONG_PRESSED&&!moved){long_press=true;if(lv_obj_has_flag(controls,LV_OBJ_FLAG_HIDDEN))lv_obj_remove_flag(controls,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);}
    if(code==LV_EVENT_RELEASED){held=false;activity=now;if(!moved&&!long_press)select_emotion(emotion+1,now);}
    if(code==LV_EVENT_PRESS_LOST){held=false;moved=true;}
}
static void action(lv_event_t *e){
    int id=(int)(intptr_t)lv_event_get_user_data(e);activity=lv_tick_get();
    if(id==0){if(go_back)go_back();return;}
    if(id==3){theme=!theme;background();lv_obj_set_style_text_color(name_label,theme?lv_color_white():lv_color_hex(0x191919),0);return;}
    select_emotion(emotion+(id==1?-1:1),activity);
}
static void control(const char *text,int x,int y,int w,int id){
    lv_obj_t *o=lv_button_create(controls);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x293542),0);lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,&font18,0);lv_obj_set_style_text_color(l,lv_color_white(),0);lv_obj_center(l);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);lv_obj_add_event_cb(o,action,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)id);
}
void grok_close(void){
    if(image){lv_obj_delete(image);image=NULL;}
    if(controls){lv_obj_delete(controls);controls=NULL;}
    lv_image_cache_drop(&picture);free(base);free(pixels);base=pixels=NULL;picture.data=NULL;held=false;go_back=NULL;
}
bool grok_open(lv_obj_t *parent,void (*back)(void)){
    grok_close();
#ifdef ESP_PLATFORM
    base=heap_caps_malloc(SIDE*SIDE*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    pixels=heap_caps_malloc(SIDE*SIDE*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    base=malloc(SIDE*SIDE*2);pixels=malloc(SIDE*SIDE*2);
#endif
    if(!base||!pixels){grok_close();return false;}
    picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=SIDE,.h=SIDE,.stride=SIDE*2},.data_size=SIDE*SIDE*2,.data=(uint8_t*)pixels};
    image=lv_image_create(parent);lv_obj_set_pos(image,0,0);lv_image_set_src(image,&picture);lv_obj_add_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(image,LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(image,touch,LV_EVENT_ALL,NULL);
    controls=lv_obj_create(parent);lv_obj_remove_style_all(controls);lv_obj_set_size(controls,360,360);lv_obj_remove_flag(controls,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    control("<",70,280,48,1);control(">",242,280,48,2);control("返回",100,24,70,0);control("配色",190,24,70,3);
    name_label=lv_label_create(controls);lv_obj_set_style_text_font(name_label,&font18,0);lv_obj_set_style_text_color(name_label,lv_color_white(),0);lv_obj_set_width(name_label,120);lv_obj_set_style_text_align(name_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(name_label,120,292);
    lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);go_back=back;gaze_x=gaze_y=want_x=want_y=0;held=moved=long_press=false;
    uint32_t now=lv_tick_get();last_tick=now;select_emotion(2,now);memcpy(current,target,sizeof(current));memcpy(start,target,sizeof(start));grok_tick(now);return true;
}

#endif
