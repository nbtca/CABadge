#include "display_perf.h"
#include "frame_trace.h"
#if CABADGE_DISPLAY_PERF
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include "ui/badge_ui.h"
#include "src/display/lv_display_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
/* GUI task only. ISR timestamps arrive through physical_display_complete.
 * Render excludes FLUSH and FLUSH_WAIT event spans, but includes preemption.
 * Completion is panel draw entry -> final DMA callback, NOT LCD scan time. */
typedef struct {
    uint32_t rendered,frames,calls,done,errors,intervals;
    uint64_t pixels,render,render_max,enqueue,enqueue_max,complete,complete_max,gaps,gap_max,wait;
} counts_t;
static counts_t window;
static struct {counts_t counts; int from,to; bool reduced,active,settled; uint32_t id; int64_t start,last; uint64_t call_us; uint64_t frame_pixels,max_pixels,idle[2];} tr;
static uint32_t sequence,flush_id;
static lv_area_t frame_box,boxes[16];
static bool box_valid;
static unsigned box_count,full_frames,partial_frames;
static uint64_t min_pixels;
static char transition_result[2048];
static uint64_t idle_counter(int core){TaskStatus_t t;vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core),&t,pdFALSE,eInvalid);return t.ulRunTimeCounter;}
static void transition_finish(bool interrupted);
static int64_t epoch,render_at,segment_at,flush_at,wait_at,previous_complete;
static uint64_t frame_render;
static void add(uint64_t *sum,uint64_t *maximum,uint64_t value){*sum+=value;if(value>*maximum)*maximum=value;}
static void pause_render(int64_t now){if(segment_at){frame_render+=now-segment_at;segment_at=0;}}
static void collect(counts_t before){
    if(!tr.active)return;
#define DELTA(f) tr.counts.f+=window.f-before.f
    DELTA(rendered);DELTA(calls);DELTA(pixels);DELTA(render);DELTA(enqueue);DELTA(wait);
#undef DELTA
    uint64_t r=window.render-before.render,e=window.enqueue-before.enqueue;
    if(r>tr.counts.render_max)tr.counts.render_max=r;
    if(e>tr.counts.enqueue_max)tr.counts.enqueue_max=e;
    tr.frame_pixels+=window.pixels-before.pixels;
    if(window.rendered!=before.rendered){
        if(box_valid){if(box_count<16)boxes[box_count++]=frame_box;if(frame_box.x1==0&&frame_box.y1==0&&frame_box.x2==359&&frame_box.y2==359)full_frames++;else partial_frames++;}
        box_valid=false;
        if(tr.counts.rendered==1||tr.frame_pixels<min_pixels)min_pixels=tr.frame_pixels;
        if(tr.frame_pixels>tr.max_pixels)tr.max_pixels=tr.frame_pixels;
        tr.frame_pixels=0;tr.settled=!badge_ui_transition_active();}
}
void display_perf_event(lv_event_t *event){
    counts_t before=window;
    int64_t now=esp_timer_get_time();
    switch(lv_event_get_code(event)){
    case LV_EVENT_REFR_READY:if(tr.active)tr.settled=!badge_ui_transition_active();break;
    case LV_EVENT_RENDER_START:render_at=segment_at=now;frame_render=0;break;
    case LV_EVENT_FLUSH_WAIT_START:pause_render(now);wait_at=now;break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        if(wait_at)window.wait+=now-wait_at;
        wait_at=0;if(render_at)segment_at=now;break;
    case LV_EVENT_FLUSH_START:{
        pause_render(now);flush_at=now;window.calls++;flush_id=tr.active?tr.id:0;
        const lv_area_t *a=lv_event_get_param(event);
        if(tr.active){
            if(!box_valid){frame_box=*a;box_valid=true;}
            else {if(a->x1<frame_box.x1)frame_box.x1=a->x1;if(a->y1<frame_box.y1)frame_box.y1=a->y1;if(a->x2>frame_box.x2)frame_box.x2=a->x2;if(a->y2>frame_box.y2)frame_box.y2=a->y2;}
        }
        window.pixels+=(uint64_t)lv_area_get_width(a)*lv_area_get_height(a);break;
    }
    case LV_EVENT_FLUSH_FINISH:
        if(flush_at)add(&window.enqueue,&window.enqueue_max,now-flush_at);
        flush_at=0;if(render_at)segment_at=now;break;
    case LV_EVENT_RENDER_READY:
        pause_render(now);if(render_at){window.rendered++;add(&window.render,&window.render_max,frame_render);}
        render_at=0;break;
    default:break;
    }
    collect(before);
}
void display_perf_complete(uint32_t us,bool last,bool ok){
    if(!ok){window.errors++;return;}
    window.done++;add(&window.complete,&window.complete_max,us);
    if(tr.active&&flush_id==tr.id){
        tr.counts.done++;add(&tr.counts.complete,&tr.counts.complete_max,us);
        if(last){int64_t now=esp_timer_get_time();tr.counts.frames++;if(tr.last){tr.counts.intervals++;add(&tr.counts.gaps,&tr.counts.gap_max,now-tr.last);}tr.last=now;}
    }
    if(last){
        int64_t now=esp_timer_get_time();window.frames++;
        /* GUI-observed completion intervals include callback-consumption delay. */
        if(previous_complete){window.intervals++;add(&window.gaps,&window.gap_max,now-previous_complete);}
        previous_complete=now;
    }
}
bool display_perf_report(char *out,size_t capacity){
    int64_t now=esp_timer_get_time();if(!epoch){epoch=now;return false;}
    if(now-epoch<1000000||render_at)return false;
    int n=snprintf(out,capacity,
        "{\"schema\":1,\"elapsed_us\":%lld,\"rendered\":%lu,\"completed_frames\":%lu,\"flush_calls\":%lu,\"flush_done\":%lu,\"errors\":%lu,"
        "\"pixels\":%llu,\"bytes\":%llu,\"render_us\":%llu,\"render_max_us\":%llu,\"enqueue_us\":%llu,\"enqueue_max_us\":%llu,"
        "\"complete_us\":%llu,\"complete_max_us\":%llu,\"intervals\":%lu,\"interval_us\":%llu,\"interval_max_us\":%llu,\"wait_us\":%llu,"
        "\"internal_free\":%u,\"internal_min\":%u,\"psram_free\":%u,\"psram_min\":%u}",
        (long long)(now-epoch),(unsigned long)window.rendered,(unsigned long)window.frames,(unsigned long)window.calls,(unsigned long)window.done,(unsigned long)window.errors,
        (unsigned long long)window.pixels,(unsigned long long)(window.pixels*2),(unsigned long long)window.render,(unsigned long long)window.render_max,
        (unsigned long long)window.enqueue,(unsigned long long)window.enqueue_max,(unsigned long long)window.complete,(unsigned long long)window.complete_max,
        (unsigned long)window.intervals,(unsigned long long)window.gaps,(unsigned long long)window.gap_max,(unsigned long long)window.wait,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    memset(&window,0,sizeof(window));epoch=now;
    return n>0&&(size_t)n<capacity;
}

static void transition_finish(bool interrupted){
    frame_trace_end();
    int64_t wall=esp_timer_get_time()-tr.start;counts_t *c=&tr.counts;
    uint64_t idle0=idle_counter(0)-tr.idle[0],idle1=idle_counter(1)-tr.idle[1];
    snprintf(transition_result,sizeof(transition_result),
      "{\"id\":%lu,\"from\":%d,\"to\":%d,\"reduce_motion\":%s,\"interrupted\":%s,\"wall_us\":%lld,"
      "\"page_call_us\":%llu,\"refresh\":%lu,\"frames\":%lu,\"render_us\":%llu,\"render_max_us\":%llu,\"pixels\":%llu,\"max_frame_pixels\":%llu,"
      "\"flush_calls\":%lu,\"bytes\":%llu,\"enqueue_us\":%llu,\"done\":%lu,\"complete_us\":%llu,\"complete_max_us\":%llu,\"worst_interval_us\":%llu,\"idle0_us\":%llu,\"idle1_us\":%llu}",
      (unsigned long)tr.id,tr.from,tr.to,tr.reduced?"true":"false",interrupted?"true":"false",(long long)wall,
      (unsigned long long)tr.call_us,(unsigned long)c->rendered,(unsigned long)c->frames,(unsigned long long)c->render,(unsigned long long)c->render_max,(unsigned long long)c->pixels,(unsigned long long)tr.max_pixels,
      (unsigned long)c->calls,(unsigned long long)c->pixels*2,(unsigned long long)c->enqueue,(unsigned long)c->done,(unsigned long long)c->complete,(unsigned long long)c->complete_max,(unsigned long long)c->gap_max,(unsigned long long)idle0,(unsigned long long)idle1);
    size_t used=strlen(transition_result)-1;
    used+=snprintf(transition_result+used,sizeof(transition_result)-used,",\"intervals\":%lu,\"interval_us\":%llu}",(unsigned long)c->intervals,(unsigned long long)c->gaps);
    used--;
    used+=snprintf(transition_result+used,sizeof(transition_result)-used,",\"full_frames\":%u,\"partial_frames\":%u,\"min_frame_pixels\":%llu,\"flush_bounds\":[",full_frames,partial_frames,(unsigned long long)min_pixels);
    for(unsigned i=0;i<box_count;i++)used+=snprintf(transition_result+used,sizeof(transition_result)-used,"%s[%d,%d,%d,%d]",i?",":"",(int)boxes[i].x1,(int)boxes[i].y1,(int)boxes[i].x2,(int)boxes[i].y2);
    snprintf(transition_result+used,sizeof(transition_result)-used,"]}");
    tr.active=false;
}
void display_perf_transition_begin(int from,int to,bool reduced){
    if(from==to)return;
    if(tr.active)transition_finish(true);
    box_valid=false;box_count=full_frames=partial_frames=0;min_pixels=0;
    memset(&tr,0,sizeof(tr));tr.id=++sequence;tr.from=from;tr.to=to;tr.reduced=reduced;tr.active=true;
    tr.start=esp_timer_get_time();frame_trace_begin(tr.id);tr.idle[0]=idle_counter(0);tr.idle[1]=idle_counter(1);
}
void display_perf_transition_call_done(void){if(tr.active)tr.call_us=esp_timer_get_time()-tr.start;}
bool display_perf_transition_report(char *out,size_t capacity,bool pending){
    /* LVGL 9.4 pauses its refresh timer when no region is dirty. A clipped
       spring tail may settle without another RENDER_READY/REFR_READY event.
       Read only: do not invalidate or force an extra refresh for measurement. */
    lv_display_t *display=lv_display_get_default();
    bool quiet=display&&!display->inv_p&&!display->rendering_in_progress&&!badge_ui_transition_active();
    if(!transition_result[0]&&tr.active&&(tr.settled||quiet)&&!pending&&tr.counts.frames>=tr.counts.rendered&&tr.counts.frames)transition_finish(false);
    if(!transition_result[0])return false;
    snprintf(out,capacity,"%s",transition_result);transition_result[0]=0;return true;
}
#endif
