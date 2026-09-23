#include "ui_transition_cache.h"
#include "ui_transition_compositor.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/misc/cache/lv_cache.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

/* Bounded metadata; pixels are lazy, PSRAM-only, and evictable. */
#define SURFACES 20
#define TRANSITION_CACHE_BUDGET_BYTES (2048u*1024u)
static size_t budget=3u*1024u*1024u;
#define BUDGET budget
static ui_memory_pressure_t pressure;
#define PSRAM_SOFT_FREE (2u*1024u*1024u)
typedef enum { INVALID,DIRTY,BUILDING,READY } cache_state_t;
typedef struct {
    lv_obj_t *real,*peer;
    lv_draw_buf_t buffer;
    cache_state_t state;
    uint32_t used,revision,cache_revision,built_at;
    unsigned page_id,priority,rank;
    bool wanted,attempted,pinned,suspended,hot,deferred;
} surface_t;
static surface_t surfaces[SURFACES];
static uint32_t clock_id,builds,failures,evictions;
static size_t occupied;
static uint64_t build_us,build_max_us;
static bool building,paused;
static unsigned planned_page=999,prewarm_builds;
static bool planned;
static bool enabled=true,direct_enabled=true;
static surface_t *direct_sources[UI_COMPOSITOR_LAYERS];
static unsigned direct_count;
static unsigned direct_hits,direct_misses,invalidations,rebuilds;
#if CABADGE_TRANSITION_CACHE_DEBUG
static struct cache_event {const char *event,*reason;unsigned page,slot,state,revision,cached,bytes,at,age;uint64_t us;} *events;
static unsigned event_read,event_write,event_lost;
#endif
static void trace(surface_t *s,const char *event,const char *reason,uint64_t us){
#if CABADGE_TRANSITION_CACHE_DEBUG
    if(!events){
#ifdef ESP_PLATFORM
        events=heap_caps_calloc(128,sizeof(*events),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        events=calloc(128,sizeof(*events));
#endif
    }
    if(!events)return;
    if(event_write-event_read==128){event_read++;event_lost++;}
    events[event_write++%128]=(struct cache_event){event,reason,s?s->page_id:999,s?(unsigned)(s-surfaces):999,s?s->state:0,s?s->revision:0,s?s->cache_revision:0,s?s->buffer.data_size:0,lv_tick_get(),s?clock_id-s->used:0,us};
#else
    (void)s;(void)event;(void)reason;(void)us;
#endif
}
bool ui_transition_cache_debug_take(char *out,size_t size){
#if CABADGE_TRANSITION_CACHE_DEBUG
    if(event_read==event_write)return false;
    const struct cache_event *e=&events[event_read++%128];
    snprintf(out,size,"{\"event\":\"%s\",\"reason\":\"%s\",\"page\":%u,\"slot\":%u,\"state\":%u,\"revision\":%u,\"cached_revision\":%u,\"bytes\":%u,\"at_ms\":%u,\"age\":%u,\"build_us\":%llu,\"lost\":%u}",e->event,e->reason,e->page,e->slot,e->state,e->revision,e->cached,e->bytes,e->at,e->age,(unsigned long long)e->us,event_lost);return true;
#else
    (void)out;(void)size;return false;
#endif
}
void ui_transition_cache_debug_dump(void){for(int i=0;i<SURFACES;i++)if(surfaces[i].real)trace(&surfaces[i],"STATE","QUERY",0);}

static uint64_t now_us(void){
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    return (uint64_t)lv_tick_get()*1000;
#endif
}
static surface_t *find(lv_obj_t *o){if(!o)return NULL;for(int i=0;i<SURFACES;i++)if(surfaces[i].real==o)return &surfaces[i];return NULL;}
static void show(lv_obj_t *o,bool on){if(on)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);}
static void release(surface_t *s){
    if(s->pinned)ui_transition_cache_direct_end();
    if(s->buffer.data){lv_image_cache_drop(&s->buffer);occupied-=s->buffer.data_size;free(s->buffer.data);memset(&s->buffer,0,sizeof(s->buffer));}
    s->state=INVALID;
}
static void deleted(lv_event_t *e){
    surface_t *s=lv_event_get_user_data(e);trace(s,"DESTROY","OBJECT_DELETE",0);
    surface_t *peer=find(s->peer);if(peer)peer->peer=NULL;release(s);memset(s,0,sizeof(*s));planned=false;
}
void ui_transition_cache_register(lv_obj_t *o,bool drag){
    (void)drag;if(!o||find(o))return;
    for(int i=0;i<SURFACES;i++)if(!surfaces[i].real){
        surface_t *s=&surfaces[i];s->real=o;s->page_id=100+i;s->revision=1;s->priority=4;s->state=INVALID;trace(s,"REGISTER","NEW_OBJECT",0);
        lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,s);planned=false;return;
    }
}
void ui_transition_cache_request(lv_obj_t *o){surface_t *s=find(o);if(s){s->used=++clock_id;s->wanted=true;s->deferred=false;s->priority=0;}}
static bool valid_cache(const surface_t *s){return s&&s->state==READY&&s->cache_revision==s->revision;}
static void invalidate_one(surface_t *s,const char *reason){
    s->revision++;invalidations++;bool was_ready=s->state==READY;
    s->state=s->buffer.data?DIRTY:INVALID;s->deferred=false;
    if(s->buffer.data)s->wanted=true;
    if(was_ready)trace(s,"INVALIDATE",reason,0);
}
void ui_transition_cache_invalidate_reason(lv_obj_t *o,const char *reason){
    if(building)return;
    uint32_t seen=0;
    for(;o;o=lv_obj_get_parent(o)){
        surface_t *s=find(o);if(!s)continue;
        surface_t *pair[2]={s,find(s->peer)};
        for(int i=0;i<2;i++)if(pair[i]){unsigned bit=1u<<(pair[i]-surfaces);if(!(seen&bit)){seen|=bit;invalidate_one(pair[i],reason);}}
    }
}
void ui_transition_cache_invalidate(lv_obj_t *o){ui_transition_cache_invalidate_reason(o,"VISUAL_CHANGED");}
void ui_transition_cache_identify(lv_obj_t *o,unsigned id){surface_t *s=find(o);if(s)s->page_id=id;}
void ui_transition_cache_keep(lv_obj_t *o,bool hot){surface_t *s=find(o);if(s)s->hot=hot;}
void ui_transition_cache_alias(lv_obj_t *a,lv_obj_t *b){
    surface_t *x=find(a),*y=find(b);if(!x||!y)return;
    x->peer=b;y->peer=a;uint32_t revision=x->revision>y->revision?x->revision:y->revision;
    x->revision=y->revision=revision;
    if(x->buffer.data&&x->cache_revision!=revision)x->state=DIRTY;
    if(y->buffer.data&&y->cache_revision!=revision)y->state=DIRTY;
}
void ui_transition_cache_plan(const unsigned *ids,unsigned count){
    if(!count||(planned&&planned_page==ids[0]))return;
    planned=true;planned_page=ids[0];
    for(int i=0;i<SURFACES;i++){surface_t *s=&surfaces[i];s->priority=4;s->rank=99;s->wanted=false;s->deferred=false;}
    for(unsigned j=0;j<count;j++)for(int i=0;i<SURFACES;i++)if(surfaces[i].real&&surfaces[i].page_id==ids[j]){
        surface_t *s=&surfaces[i];unsigned priority=j==0?0:j<4?1:2;
        if(priority<s->priority){s->priority=priority;s->rank=j;s->wanted=true;}
    }
}
void ui_transition_cache_pause(bool pause){paused=pause;planned=false;}
bool ui_transition_cache_visible(lv_obj_t *o){return o&&!lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN);}
void ui_transition_cache_show(lv_obj_t *o,bool on){if(o)show(o,on);}
void ui_transition_cache_position(lv_obj_t *o){(void)o;}
bool ui_transition_cache_begin(lv_obj_t *o){
    surface_t *s=find(o);if(s){s->attempted=true;ui_transition_cache_request(o);}
    /* Native-only pixels are never supplied to lv_image. A MISS uses real widgets. */
    return false;
}
void ui_transition_cache_end(lv_obj_t *o){surface_t *s=find(o);if(s)s->attempted=false;}
void ui_transition_cache_forget(lv_obj_t *o){surface_t *s=find(o);if(!s)return;ui_transition_cache_end(o);trace(s,"FREE","EXPLICIT_FORGET",0);release(s);s->wanted=false;planned=false;}
static surface_t *victim(surface_t *incoming,bool under_pressure){
    surface_t *old=NULL;
    for(int i=0;i<SURFACES;i++){
        surface_t *v=&surfaces[i];if(v==incoming||!v->buffer.data||v->pinned||v->attempted)continue;
        if(under_pressure&&pressure==UI_MEMORY_HIGH&&(v->hot||v->priority==0))continue;
        if(!under_pressure&&incoming&&incoming->priority>0&&valid_cache(v)&&(v->hot||v->priority<=incoming->priority))continue;
        if(!old||(!valid_cache(v)&&valid_cache(old))||
           (valid_cache(v)==valid_cache(old)&&(v->hot<old->hot||(v->hot==old->hot&&(v->priority>old->priority||(v->priority==old->priority&&v->used<old->used))))))old=v;
    }
    return old;
}
void ui_transition_cache_reserve(size_t free_bytes){
#ifdef ESP_PLATFORM
    ui_transition_cache_direct_end();
    while(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<free_bytes){
        surface_t *v=victim(NULL,true);if(!v)break;
        trace(v,"EVICT","APP_MEMORY_RESERVE",0);release(v);v->wanted=false;evictions++;
    }
#else
    (void)free_bytes;
#endif
    planned=false;
}
ui_memory_pressure_t ui_memory_pressure_get(void){return pressure;}
void ui_memory_pressure_set(ui_memory_pressure_t value){
    if(pressure==value&&occupied<=budget)return;
    pressure=value;budget=value==UI_MEMORY_NORMAL?3u*1024u*1024u:value==UI_MEMORY_HIGH?1024u*1024u:512u*1024u;
    planned=false;
    while(occupied>budget){
        surface_t *v=victim(NULL,true);if(!v)break;
        trace(v,"EVICT","MEMORY_PRESSURE",0);release(v);v->wanted=false;evictions++;
    }
}
void ui_transition_cache_trim(void){
    ui_transition_cache_direct_end();
    for(int i=0;i<SURFACES;i++)if(surfaces[i].real&&!surfaces[i].attempted){trace(&surfaces[i],"FREE","EXPLICIT_TRIM",0);release(&surfaces[i]);surfaces[i].wanted=false;}
    planned=false;
}
bool ui_transition_cache_active(void){for(int i=0;i<SURFACES;i++)if(surfaces[i].attempted)return true;return false;}
void ui_transition_cache_poll(bool idle){
    if(pressure!=UI_MEMORY_NORMAL){ui_memory_pressure_set(pressure);return;}
    if(!enabled||paused||!idle||building||direct_count||ui_transition_cache_active()||lv_anim_count_running())return;
    for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))if(lv_indev_get_state(in)==LV_INDEV_STATE_PRESSED)return;
    surface_t *s=NULL;
    for(int i=0;i<SURFACES;i++){surface_t *v=&surfaces[i];if(v->real&&!v->suspended&&v->wanted&&!v->deferred&&!valid_cache(v)&&!v->attempted&&(!s||v->priority<s->priority||(v->priority==s->priority&&v->rank<s->rank)))s=v;}
    if(!s)return;
    lv_obj_update_layout(s->real);
    /* Unsupported ext-draw/transparent roots safely retain widget transitions. */
    if(lv_obj_get_ext_draw_size(s->real)||lv_obj_get_style_bg_opa(s->real,0)!=LV_OPA_COVER){s->wanted=false;failures++;return;}
    int w=lv_obj_get_width(s->real),h=lv_obj_get_height(s->real);
    uint32_t stride=lv_draw_buf_width_to_stride(w,LV_COLOR_FORMAT_RGB565),bytes=stride*h;
    if(!w||!h||bytes>BUDGET){s->wanted=false;failures++;return;}
    if(s->buffer.data&&(s->buffer.header.w!=w||s->buffer.header.h!=h))release(s);
    if(!s->buffer.data){
        while(occupied+bytes>BUDGET){
            surface_t *old=victim(s,false);
            if(!old){s->deferred=true;return;}
            trace(old,"EVICT","LRU_BUDGET",0);release(old);old->wanted=false;evictions++;
        }
#ifdef ESP_PLATFORM
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<PSRAM_SOFT_FREE+bytes){
            surface_t *old=victim(s,false);
            if(old){trace(old,"EVICT","SOFT_PRESSURE",0);release(old);old->wanted=false;evictions++;}
            s->deferred=true;return;
        }
        void *data=heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        void *data=malloc(bytes);
#endif
        if(!data){s->wanted=false;failures++;return;}
        if(lv_draw_buf_init(&s->buffer,w,h,LV_COLOR_FORMAT_RGB565,stride,data,bytes)!=LV_RESULT_OK){free(data);s->wanted=false;failures++;return;}occupied+=bytes;
    }
    bool rebuilding=s->cache_revision!=0;building=true;s->state=BUILDING;bool hidden=lv_obj_has_flag(s->real,LV_OBJ_FLAG_HIDDEN);
    show(s->real,true);lv_image_cache_drop(&s->buffer);uint64_t started=now_us();
    lv_result_t result=lv_snapshot_take_to_draw_buf(s->real,LV_COLOR_FORMAT_RGB565,&s->buffer);
    uint64_t elapsed=now_us()-started;show(s->real,!hidden);building=false;
    s->wanted=false;
    if(result!=LV_RESULT_OK){s->state=INVALID;failures++;return;}
    uint8_t *native=s->buffer.data;
    for(uint32_t i=0;i<bytes;i+=2){uint8_t byte=native[i];native[i]=native[i+1];native[i+1]=byte;}
    elapsed=now_us()-started;build_us+=elapsed;if(elapsed>build_max_us)build_max_us=elapsed;
    prewarm_builds++;s->state=READY;s->cache_revision=s->revision;s->built_at=lv_tick_get();builds++;if(rebuilding)rebuilds++;trace(s,"BUILD",rebuilding?"REBUILD":"FIRST_BUILD",elapsed);
}
void ui_transition_cache_stats(char *out,size_t size){
    unsigned ready=0,active=0;for(int i=0;i<SURFACES;i++){ready+=surfaces[i].real&&surfaces[i].state==READY;active+=surfaces[i].pinned;}
    snprintf(out,size,"{\"hits\":%lu,\"misses\":%lu,\"fallbacks\":%lu,\"builds\":%lu,\"failures\":%lu,\"build_us\":%llu,\"build_max_us\":%llu,\"bytes\":%u,\"budget\":%u,\"evictions\":%lu,\"ready\":%u,\"active\":%u}",
        (unsigned long)direct_hits,(unsigned long)direct_misses,(unsigned long)direct_misses,(unsigned long)builds,(unsigned long)failures,(unsigned long long)build_us,(unsigned long long)build_max_us,(unsigned)occupied,BUDGET,(unsigned long)evictions,ready,active);
    size_t used=strlen(out);if(used&&used<size-1)snprintf(out+used-1,size-used+1,",\"direct_hits\":%u,\"direct_misses\":%u,\"invalidations\":%u,\"rebuilds\":%u,\"prewarm_builds\":%u,\"copies\":1,\"l2_bytes\":0}",direct_hits,direct_misses,invalidations,rebuilds,prewarm_builds);
}
#if CABADGE_DISPLAY_PERF
void ui_transition_cache_test_mode(unsigned mode){
    /* Diagnostic only: 0 normal, 1 identical widget fallback, 2 cold-cache. */
    for(int i=0;i<SURFACES;i++)if(surfaces[i].real){
        ui_transition_cache_end(surfaces[i].real);
        if(mode==2){release(&surfaces[i]);surfaces[i].wanted=false;}
    }
    ui_transition_cache_direct_end();enabled=mode!=1;direct_enabled=mode!=3;
}
#endif

bool ui_transition_cache_direct_active(void){return direct_count&&ui_transition_compositor_active();}
void ui_transition_cache_direct_end(void){
    bool was_active=direct_count!=0;ui_transition_compositor_stop();
    for(unsigned i=0;i<direct_count;i++)direct_sources[i]->pinned=false;
    direct_count=0;if(was_active)planned=false;
}
bool ui_transition_cache_direct(lv_obj_t *const *objects,const int *xy,unsigned count,int x,int y,int width,int height,uint16_t background){
    #ifdef ESP_PLATFORM
    extern bool app_service_map_busy(void);
    if(app_service_map_busy())return false;
#endif
    if(!enabled||!direct_enabled||!count||count>UI_COMPOSITOR_LAYERS)return false;
    surface_t *sources[UI_COMPOSITOR_LAYERS];bool same=count==direct_count;
    for(unsigned i=0;i<count;i++){sources[i]=find(objects[i]);same=same&&sources[i]==direct_sources[i];}
    if(!same&&direct_count)ui_transition_cache_direct_end();
    ui_compositor_frame_t f={.x=x,.y=y,.width=width,.height=height,.background=background,.count=count};
    for(unsigned i=0;i<count;i++){
        surface_t *s=sources[i];
        if(!s||(!s->pinned&&!valid_cache(s))){ui_transition_cache_direct_end();if(s)ui_transition_cache_request(s->real);direct_misses++;trace(s,"TRANS_FALLBACK",!s?"UNREGISTERED":s->state==DIRTY?"VISUAL_STALE":"CACHE_MISS",0);return false;}
        s->used=++clock_id;
        f.layers[i]=(ui_compositor_layer_t){.pixels=s->buffer.data,.stride=s->buffer.header.stride,
            .width=s->buffer.header.w,.height=s->buffer.header.h,.x=xy[i*2],.y=xy[i*2+1],.radius=lv_obj_get_style_radius(s->real,0)};
    }
    if(direct_count){
        ui_transition_compositor_present(&f);
        if(!ui_transition_compositor_active()){ui_transition_cache_direct_end();return false;}
        return true;
    }
    for(unsigned i=0;i<count;i++)sources[i]->pinned=true;
    if(!ui_transition_compositor_begin(&f)){trace(sources[0],"TRANS_FALLBACK","DISPLAY_OR_STAGING_UNAVAILABLE",0);direct_misses++;for(unsigned i=0;i<count;i++)sources[i]->pinned=false;return false;}
    trace(sources[count-1],"TRANS_DIRECT","READY",0);direct_hits++;direct_count=count;for(unsigned i=0;i<count;i++)direct_sources[i]=sources[i];return true;
}

bool ui_transition_cache_ready(lv_obj_t *o){surface_t *s=find(o);return valid_cache(s);}
void ui_transition_cache_suspend(lv_obj_t *o,bool suspended,bool retain){surface_t *s=find(o);(void)retain;if(s&&s->suspended!=suspended){s->suspended=suspended;planned=false;}}
