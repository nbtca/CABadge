"""Run the production worker on Windows threads with a deliberately blocked DMA."""
from pathlib import Path
import os, subprocess

root=Path(__file__).resolve().parent
stage=Path('F:/CABadgeBuild/temp/v7-async-worker-check')
stage.mkdir(parents=True,exist_ok=True)
for name in ('lvgl.h','esp_err.h','freertos/FreeRTOS.h','freertos/queue.h','freertos/task.h'):
    p=stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('/* supplied by check.c */\n')
unit=stage/'check.c'
unit.write_text(r'''
#include <windows.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define configASSERT(x) assert(x)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_STATE 259
typedef int BaseType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY INFINITE
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int unused;} lv_indev_t;
typedef struct {int unused;} lv_indev_data_t;
typedef struct {CRITICAL_SECTION lock;CONDITION_VARIABLE changed;unsigned used,size;uint8_t data[128];} *QueueHandle_t;
static QueueHandle_t xQueueCreate(unsigned count,unsigned size){
    assert(count==1&&size<=128);QueueHandle_t q=calloc(1,sizeof(*q));assert(q);
    InitializeCriticalSection(&q->lock);InitializeConditionVariable(&q->changed);q->size=size;return q;
}
static int xQueueSend(QueueHandle_t q,const void *data,DWORD timeout){
    EnterCriticalSection(&q->lock);
    while(q->used){if(!timeout){LeaveCriticalSection(&q->lock);return 0;}assert(SleepConditionVariableCS(&q->changed,&q->lock,timeout));}
    memcpy(q->data,data,q->size);q->used=1;WakeAllConditionVariable(&q->changed);LeaveCriticalSection(&q->lock);return 1;
}
static int xQueueReceive(QueueHandle_t q,void *data,DWORD timeout){
    EnterCriticalSection(&q->lock);
    while(!q->used){if(!timeout){LeaveCriticalSection(&q->lock);return 0;}assert(SleepConditionVariableCS(&q->changed,&q->lock,timeout));}
    memcpy(data,q->data,q->size);q->used=0;WakeAllConditionVariable(&q->changed);LeaveCriticalSection(&q->lock);return 1;
}
static void (*task_fn)(void*);static HANDLE worker_handle;
static DWORD WINAPI thread_main(void *unused){task_fn(unused);return 0;}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,int stack,void *arg,int priority,void *handle,int core){
    assert(!strcmp(name,"badge_lcd")&&stack>=3072&&priority==3&&core==1);
    (void)handle;task_fn=fn;worker_handle=CreateThread(NULL,0,thread_main,arg,0,NULL);assert(worker_handle);return pdPASS;
}
#include "display_worker.c"

static HANDLE began,finish_dma;static unsigned calls;static int simulated_error;
static uint8_t *expected;static lv_area_t expected_area;
void physical_display_flush(const lv_area_t *area,const uint8_t *pixels){
    assert(pixels==expected&&!memcmp(area,&expected_area,sizeof(*area)));
    uint8_t before[128];memcpy(before,pixels,sizeof(before));
    SetEvent(began);assert(WaitForSingleObject(finish_dma,2000)==WAIT_OBJECT_0);
    assert(!memcmp(before,pixels,sizeof(before)));calls++;
}
uint32_t physical_display_flush_us(void){return 1200;}
uint32_t physical_display_copy_us(void){return 70;}
lcd_profile_t physical_display_profile_result(void){return (lcd_profile_t){10,900,110};}
int physical_display_error(void){return simulated_error;}
int main(void){
    began=CreateEvent(NULL,FALSE,FALSE,NULL);finish_dma=CreateEvent(NULL,FALSE,FALSE,NULL);assert(began&&finish_dma);
    uint8_t buffers[2][128];memset(buffers[0],0x12,128);memset(buffers[1],0x34,128);
    display_worker_init();
    for(unsigned i=0;i<2;i++){
        expected=buffers[i];expected_area=(lv_area_t){13,17,28,20};lv_area_t area=expected_area;
        simulated_error=i?7:0;display_worker_submit(&area,expected,i==1);area.x1=999;
        assert(WaitForSingleObject(began,2000)==WAIT_OBJECT_0);
        display_result_t premature;assert(!xQueueReceive(results,&premature,0));
        /* The next draw may proceed while this transfer still owns its buffer. */
        memset(buffers[i^1],0x56+i,128);assert(calls==i);
        SetEvent(finish_dma);display_result_t result=display_worker_wait();
        assert(calls==i+1&&result.error==simulated_error&&result.last==(i==1));
        assert(result.flush_us==1200&&result.copy_us==70&&result.dma.wire_us==900);
    }
    puts("Production display worker: asynchronous ownership, rectangle copy, completion and error propagation PASS");
    /* Process teardown stops the perpetual firmware worker, as a device reset does. */
    return 0;
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',
    ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=stage/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc','-I',str(stage),'-I',str(root/'usb_screen/device/src'),str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True,timeout=10)
