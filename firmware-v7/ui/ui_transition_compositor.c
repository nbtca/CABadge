#include "ui_transition_compositor.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static int maximum(int a,int b){return a>b?a:b;}
static int minimum(int a,int b){return a<b?a:b;}
size_t ui_transition_compositor_rows(const ui_compositor_frame_t *f,int first,int rows,uint8_t *out){
    size_t copied=0;
    for(int row=first;row<first+rows;row++){
        int y=f->y+row,left[UI_COMPOSITOR_LAYERS],right[UI_COMPOSITOR_LAYERS];
        int cuts[2+2*UI_COMPOSITOR_LAYERS]={f->x,f->x+f->width},n=2;
        for(unsigned i=0;i<f->count;i++){
            const ui_compositor_layer_t *l=&f->layers[i];int sy=y-l->y;
            left[i]=right[i]=f->x;
            if(sy<0||sy>=l->height)continue;
            int inset=0,r=minimum(l->radius,minimum(l->width,l->height)/2);
            int edge=minimum(sy,l->height-1-sy);
            if(r>0&&edge<r){float dy=r-edge-.5f;inset=(int)ceilf(r-sqrtf(r*r-dy*dy)-.5f);}
            left[i]=maximum(f->x,l->x+inset);right[i]=minimum(f->x+f->width,l->x+l->width-inset);
            if(left[i]<right[i]){cuts[n++]=left[i];cuts[n++]=right[i];}
        }
        for(int i=1;i<n;i++){int v=cuts[i],j=i;while(j&&cuts[j-1]>v){cuts[j]=cuts[j-1];j--;}cuts[j]=v;}
        for(int k=1;k<n;k++){
            int a=cuts[k-1],b=cuts[k];if(a==b)continue;int layer=-1;
            for(unsigned i=0;i<f->count;i++)if(a>=left[i]&&b<=right[i])layer=(int)i;
            uint8_t *dst=out+((row-first)*f->width+a-f->x)*2;size_t bytes=(b-a)*2;
            if(layer>=0){const ui_compositor_layer_t *l=&f->layers[layer];
                memcpy(dst,l->pixels+(y-l->y)*l->stride+(a-l->x)*2,bytes);copied+=bytes;
            }else for(int x=0;x<b-a;x++)memcpy(dst+x*2,&f->background,2);
        }
    }
    return copied;
}
#ifdef ESP_PLATFORM
#include "physical_display.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#define BLOCK_ROWS PHYSICAL_STAGING_ROWS
static QueueHandle_t queue;
static SemaphoreHandle_t stopped;
static TaskHandle_t worker;
static atomic_bool active,cancelled,failed;
static uint8_t *blocks[2];
static struct {uint64_t prep,prep_max,frame_prep_max,submit,drain,lcd,lcd_max,interval,interval_max,bytes,copy,wall;uint32_t frames,attempts,transactions,skipped,errors;} perf;
static int64_t started,last_completed;
static int64_t perf_now(void){
#if CABADGE_TRANSITION_COMPOSITOR_PERF
    return esp_timer_get_time();
#else
    return 0;
#endif
}

static bool valid(const ui_compositor_frame_t *f){
    if(!f||!f->count||f->count>UI_COMPOSITOR_LAYERS||f->x<0||f->y<0||f->width<=0||f->height<=0||f->x+f->width>360||f->y+f->height>360)return false;
    for(unsigned i=0;i<f->count;i++){const ui_compositor_layer_t *l=&f->layers[i];if(!l->pixels||l->width<=0||l->height<=0||l->stride<(unsigned)l->width*2)return false;}
    return true;
}
static void run(void *arg){
    (void)arg;ui_compositor_frame_t f;
    for(;;){
        xQueueReceive(queue,&f,portMAX_DELAY);
        if(atomic_load(&cancelled)){
            /* Only the explicit sentinel acknowledges stop. A queued ordinary
             * frame may be dequeued just before cancellation; acknowledging it
             * too would leave a stale semaphore token for the next transition. */
            if(!f.count){physical_display_transition_drain();xSemaphoreGive(stopped);}
            continue;
        }
        esp_err_t rc=ESP_OK;uint64_t prep_before=perf.prep;int submitted_rows=0;int64_t lcd_start=0;perf.attempts++;
        for(int y=0;y<f.height&&!atomic_load(&cancelled);y+=BLOCK_ROWS){
            int rows=minimum(BLOCK_ROWS,f.height-y);uint8_t *block=blocks[(y/BLOCK_ROWS)&1];
            /* draw_bitmap's next address command drains the preceding transfer.
             * Alternating blocks allows packing B while DMA still reads A. */
            int64_t at=perf_now();size_t copied=ui_transition_compositor_rows(&f,y,rows,block);
            uint64_t elapsed=perf_now()-at;perf.prep+=elapsed;if(elapsed>perf.prep_max)perf.prep_max=elapsed;
            perf.copy+=copied;at=perf_now();
            if(!lcd_start)lcd_start=at;
            rc=physical_display_transition_draw(f.x,f.y+y,f.x+f.width,f.y+y+rows,block);
            perf.submit+=perf_now()-at;perf.transactions++;perf.bytes+=(uint64_t)f.width*rows*2;
            if(rc!=ESP_OK)break;
            submitted_rows+=rows;
        }
        int64_t at=perf_now();esp_err_t drained=physical_display_transition_drain();perf.drain+=perf_now()-at;
        uint64_t prep=perf.prep-prep_before;if(prep>perf.frame_prep_max)perf.frame_prep_max=prep;
        if(lcd_start){uint64_t lcd=perf_now()-lcd_start;perf.lcd+=lcd;if(lcd>perf.lcd_max)perf.lcd_max=lcd;}
        if(rc!=ESP_OK||drained!=ESP_OK){perf.errors++;atomic_store(&failed,true);}
        else if(submitted_rows==f.height){
            int64_t now=perf_now();if(last_completed){uint64_t d=now-last_completed;perf.interval+=d;if(d>perf.interval_max)perf.interval_max=d;}
            last_completed=now;perf.frames++;
        }
    }
}
bool ui_transition_compositor_active(void){return atomic_load(&active);}
bool ui_transition_compositor_begin(const ui_compositor_frame_t *f){
    if(ui_transition_compositor_active()||!valid(f))return false;
    /* Preserve a DMA reserve; cache fallback remains available without staging. */
    if(heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)<16*1024)return false;
    if(!queue)queue=xQueueCreate(1,sizeof(*f));
    if(!stopped)stopped=xSemaphoreCreateBinary();
    if(!queue||!stopped)return false;
    if(!worker&&xTaskCreate(run,"transition",4096,NULL,4,&worker)!=pdPASS)return false;
    if(!physical_display_transition_acquire())return false;
    blocks[0]=physical_display_transition_buffer(0);blocks[1]=physical_display_transition_buffer(1);
    if(!blocks[0]||!blocks[1]){physical_display_transition_release();return false;}
    memset(&perf,0,sizeof(perf));started=perf_now();last_completed=0;
    atomic_store(&cancelled,false);atomic_store(&failed,false);atomic_store(&active,true);
    xQueueOverwrite(queue,f);return true;
}
void ui_transition_compositor_present(const ui_compositor_frame_t *f){
    if(!ui_transition_compositor_active()||!valid(f))return;
    if(atomic_load(&failed)){ui_transition_compositor_stop();return;}
    if(uxQueueMessagesWaiting(queue))perf.skipped++;
    xQueueOverwrite(queue,f);
}
#if CABADGE_TRANSITION_COMPOSITOR_PERF
void ui_transition_compositor_report(const char *json);
#endif
void ui_transition_compositor_stop(void){
    if(!ui_transition_compositor_active())return;
    atomic_store(&cancelled,true);ui_compositor_frame_t stop={0};xQueueOverwrite(queue,&stop);
    xSemaphoreTake(stopped,portMAX_DELAY);
    physical_display_transition_release();atomic_store(&active,false);
    blocks[0]=blocks[1]=NULL;perf.wall=perf_now()-started;
#if CABADGE_TRANSITION_COMPOSITOR_PERF
    char summary[1024];ui_transition_compositor_stats(summary,sizeof(summary));ui_transition_compositor_report(summary);
#endif
}
void ui_transition_compositor_stats(char *out,size_t size){
    /* Read after stop; worker is the sole writer of completed-frame metrics. */
    if(ui_transition_compositor_active()){snprintf(out,size,"{\"active\":true}");return;}
    snprintf(out,size,"{\"active\":false,\"frames\":%lu,\"wall_us\":%llu,\"prep_us\":%llu,\"block_prep_max_us\":%llu,\"submit_us\":%llu,\"drain_us\":%llu,\"interval_us\":%llu,\"interval_max_us\":%llu,\"bytes\":%llu,\"copy_bytes\":%llu,\"transactions\":%lu,\"skipped\":%lu,\"errors\":%lu}",
        (unsigned long)perf.frames,(unsigned long long)perf.wall,(unsigned long long)perf.prep,(unsigned long long)perf.prep_max,(unsigned long long)perf.submit,(unsigned long long)perf.drain,(unsigned long long)perf.interval,(unsigned long long)perf.interval_max,(unsigned long long)perf.bytes,(unsigned long long)perf.copy,(unsigned long)perf.transactions,(unsigned long)perf.skipped,(unsigned long)perf.errors);
    size_t used=strlen(out);if(used&&used<size-1)snprintf(out+used-1,size-used+1,",\"attempts\":%lu,\"frame_prep_max_us\":%llu,\"lcd_span_us\":%llu,\"lcd_span_max_us\":%llu}",
        (unsigned long)perf.attempts,(unsigned long long)perf.frame_prep_max,(unsigned long long)perf.lcd,(unsigned long long)perf.lcd_max);
}
#else
bool ui_transition_compositor_begin(const ui_compositor_frame_t *f){(void)f;return false;}
void ui_transition_compositor_present(const ui_compositor_frame_t *f){(void)f;}
void ui_transition_compositor_stop(void){}
bool ui_transition_compositor_active(void){return false;}
void ui_transition_compositor_stats(char *out,size_t size){snprintf(out,size,"{}");}
#endif
