#include "ui/ui_transition_compositor.h"
#include "runtime_monitor.h"
#include "physical_display.h"
#include "ui/badge_ui.h"
#include "ui/apps.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool hud_update,content_dirty,content_frame,enabled_before;
static uint32_t rendered,submitted,last_rendered,last_submitted,window_at,diagnostic_at;
static uint64_t work_us[MON_GROUPS],render_us,bus_us;
static int64_t render_at;
static uint32_t frame_blocked_us;
static bool measuring(void){return badge_ui_perf_enabled()||(diagnostic_at&&(uint32_t)(lv_tick_get()-diagnostic_at)<5000);}
int64_t runtime_begin(void){return diagnostic_at&&(uint32_t)(lv_tick_get()-diagnostic_at)<5000?esp_timer_get_time():0;}
void runtime_end(unsigned group,int64_t start){if(start&&group<MON_GROUPS)work_us[group]+=esp_timer_get_time()-start;}
void runtime_event(lv_event_t *event){
    if(!measuring())return;
    lv_event_code_t code=lv_event_get_code(event);
    /* LVGL 9.4 also sends this event from get_max_row during rendering to
       query buffer rounding. That synthetic area is not new dirty content. */
    if(code==LV_EVENT_INVALIDATE_AREA&&!hud_update&&!render_at){
        const lv_area_t *area=lv_event_get_param(event);
        if(!badge_ui_perf_covers(area))content_dirty=true;
    }
    if(code==LV_EVENT_RENDER_START){content_frame=content_dirty;content_dirty=false;render_at=esp_timer_get_time();frame_blocked_us=0;}
    if(code==LV_EVENT_RENDER_READY&&render_at){
        if(content_frame){rendered++;uint64_t elapsed=esp_timer_get_time()-render_at;render_us+=elapsed>frame_blocked_us?elapsed-frame_blocked_us:0;}
        render_at=0;
    }
}
bool runtime_content_frame(void){return measuring()&&content_frame;}
void runtime_flush(uint32_t us,uint32_t blocked,bool last,bool ok,bool content){
    if(!measuring())return;
    /* A previous frame can complete while the current frame is being drawn. */
    if(render_at)frame_blocked_us+=blocked;
    if(content)bus_us+=us;
    if(last&&ok&&content)submitted++;
}
void runtime_poll(void){
    bool enabled=badge_ui_perf_enabled();uint32_t now=lv_tick_get();
    if(enabled!=enabled_before){enabled_before=enabled;window_at=now;last_rendered=rendered;last_submitted=submitted;}
    if(!enabled||badge_ui_is_asleep()||now-window_at<1000)return;
    char text[48];uint32_t elapsed=now-window_at;
    unsigned r=(rendered-last_rendered)*10000u/elapsed,t=(submitted-last_submitted)*10000u/elapsed;
    snprintf(text,sizeof(text),"UI %u.%u | LCD %u.%u",r/10,r%10,t/10,t%10);
    /* The meter's own invalidations must not manufacture an idle-page FPS. */
    hud_update=true;badge_ui_perf_text(text);hud_update=false;
    window_at=now;last_rendered=rendered;last_submitted=submitted;
}
static bool append(char **p,size_t *left,const char *format,...){
    va_list args;va_start(args,format);int n=vsnprintf(*p,*left,format,args);va_end(args);
    if(n<0||(size_t)n>=*left)return false;
    *p+=n;*left-=n;return true;
}
bool runtime_snapshot(char *out,size_t capacity){
    int64_t started=esp_timer_get_time();diagnostic_at=lv_tick_get();
    /* No new task or internal-RAM scratch array; allocated only while sampling. */
    TaskStatus_t *tasks=heap_caps_calloc(32,sizeof(*tasks),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!tasks)return false;
    configRUN_TIME_COUNTER_TYPE total=0;unsigned count=uxTaskGetSystemState(tasks,32,&total);
    char *p=out;size_t left=capacity;bool ok=count>0;
#define ADD(...) do{if(ok)ok=append(&p,&left,__VA_ARGS__);}while(0)
    ADD("{\"firmware\":\"" UI_VERSION "\",\"us\":%llu,\"cores\":%u,\"page\":%d,\"hud\":%s,\"asleep\":%s,\"rendered\":%lu,\"submitted\":%lu,\"render_us\":%llu,\"bus_us\":%llu,\"heap\":[",(unsigned long long)total,configNUMBER_OF_CORES,badge_ui_current_page(),badge_ui_perf_enabled()?"true":"false",badge_ui_is_asleep()?"true":"false",(unsigned long)rendered,(unsigned long)submitted,(unsigned long long)render_us,(unsigned long long)bus_us);
    const uint32_t caps[]={MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL};
    for(unsigned i=0;i<3;i++){
        multi_heap_info_t h;heap_caps_get_info(&h,caps[i]);
        ADD("%s{\"free\":%u,\"allocated\":%u,\"minimum_free\":%u,\"largest\":%u}",i?",":"",(unsigned)h.total_free_bytes,(unsigned)h.total_allocated_bytes,(unsigned)h.minimum_free_bytes,(unsigned)h.largest_free_block);
    }
    ADD("],\"panel\":%s,\"work_us\":[",physical_display_readback());for(unsigned i=0;i<MON_GROUPS;i++)ADD("%s%llu",i?",":"",(unsigned long long)work_us[i]);
    ADD("],\"tasks\":[");
    for(unsigned i=0;i<count;i++){
        TaskStatus_t *t=&tasks[i];
        ADD("%s{\"id\":%u,\"name\":\"%.15s\",\"core\":%d,\"priority\":%u,\"state\":%d,\"cpu_us\":%llu,\"stack_free_min\":%u}",i?",":"",(unsigned)t->xTaskNumber,t->pcTaskName,(int)t->xCoreID,(unsigned)t->uxCurrentPriority,t->eCurrentState,(unsigned long long)t->ulRunTimeCounter,(unsigned)t->usStackHighWaterMark*(unsigned)sizeof(StackType_t));
    }
    int app,loading,error,tiles,players;badge_apps_status(&app,&loading,&error,&tiles,&players);
    ADD("],\"apps\":{\"page\":%d,\"loading\":%d,\"error\":%d,\"tiles\":%d,\"players\":%d}",app,loading,error,tiles,players);
    ADD(",\"backlight_pwm\":%u",physical_display_backlight_duty());
    char compositor[1024];ui_transition_compositor_stats(compositor,sizeof(compositor));ADD(",\"compositor\":%s",compositor);
    char cache[512];badge_ui_connection_cache_stats(cache,sizeof(cache));ADD(",\"transition_cache\":%s,\"connection_cache\":%s",cache,cache);
    free(tasks);ADD(",\"query_us\":%llu}",(unsigned long long)(esp_timer_get_time()-started));
#undef ADD
    return ok;
}
