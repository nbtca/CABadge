#include "fluidbox.h"
#include "fluidbox/config.h"
#include "fluidbox/input.h"
#include "fluidbox/render.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *image,*mode_label;
static lv_image_dsc_t picture;
static uint16_t *front,*back;
static TaskHandle_t solver,renderer;
static SemaphoreHandle_t exited;
static atomic_bool stop,awake,ready,reset;
static atomic_uint steps,render_us,sim_us,sim_stack,render_stack;
static atomic_int mode;
static bool initialized,inflight;
static uint32_t frames,input_at;
static fluidbox_input_t input;
static sim_forces_t forces;
static portMUX_TYPE force_lock=portMUX_INITIALIZER_UNLOCKED;
static const char *const names[]={"Particles","Liquid","Water","Lava","Ukiyo-e"};

void fluidbox_motion(const int16_t xyz[3],bool available){
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    if(!available){input.primed=false;return;}
    sim_forces_t next=fluidbox_input(&input,xyz,(now-input_at)/1000.f);
    input_at=now;
    taskENTER_CRITICAL(&force_lock);forces=next;taskEXIT_CRITICAL(&force_lock);
}
static sim_forces_t current_forces(void){
    sim_forces_t out;
    taskENTER_CRITICAL(&force_lock);out=forces;taskEXIT_CRITICAL(&force_lock);
    // Read validity on the GUI side; workers use the coherent published force.
    return out;
}
static void solve(void *arg){
    (void)arg;int64_t previous=esp_timer_get_time();
    while(!atomic_load(&stop)){
        int64_t now=esp_timer_get_time();float dt=(now-previous)*1e-6f;previous=now;
        if(atomic_load(&awake)){
            if(atomic_exchange(&reset,false))sim_reset();
            sim_forces_t f=current_forces();render_set_gravity(f.gravity);
            if(dt>.05f)dt=.05f;
            if(dt<.0001f)dt=.0001f;
            int64_t start=esp_timer_get_time();
            sim_step(atomic_load(&mode)==RENDER_LAVA?dt*LAVA_TIME_SCALE:dt,&f);
            atomic_store(&sim_us,esp_timer_get_time()-start);atomic_fetch_add(&steps,1);
            atomic_store(&sim_stack,uxTaskGetStackHighWaterMark(NULL));
        }
        vTaskDelay(pdMS_TO_TICKS(atomic_load(&awake)?1:20));
    }
    xSemaphoreGive(exited);vTaskDelete(NULL);
}
void display_stage_band(int band,int left,int right,const uint16_t *pixels){
    configASSERT(band>=0&&band<BAND_COUNT&&left>=0&&right<=LCD_H_RES&&right>left);
    for(int y=0;y<BAND_ROWS;y++)memcpy(back+(band*BAND_ROWS+y)*LCD_H_RES+left,
        pixels+y*(right-left),(right-left)*sizeof(uint16_t));
}
void display_present(void){}
static void draw(void *arg){
    (void)arg;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(atomic_load(&stop))break;
        // The GUI owns front until publishing ready; LVGL DMA owns separate draw buffers.
        memcpy(back,front,LCD_H_RES*LCD_V_RES*sizeof(uint16_t));
        int64_t start=esp_timer_get_time();render_frame(true);
        atomic_store(&render_us,esp_timer_get_time()-start);
        atomic_store(&render_stack,uxTaskGetStackHighWaterMark(NULL));
        atomic_store(&ready,true);
    }
    xSemaphoreGive(exited);vTaskDelete(NULL);
}
void fluidbox_mode(int value){
    if(!image||value<0||value>=RENDER_MODE_COUNT)return;
    atomic_store(&mode,value);render_set_mode(value);lv_label_set_text(mode_label,names[value]);
}
void fluidbox_reset(void){if(image)atomic_store(&reset,true);}
void fluidbox_next(void){fluidbox_mode((atomic_load(&mode)+1)%RENDER_MODE_COUNT);}
void fluidbox_close(void){
    atomic_store(&stop,true);atomic_store(&awake,false);
    if(renderer)xTaskNotifyGive(renderer);
    if(solver)xSemaphoreTake(exited,portMAX_DELAY);
    if(renderer)xSemaphoreTake(exited,portMAX_DELAY);
    solver=renderer=NULL;
    if(exited){vSemaphoreDelete(exited);exited=NULL;}
    if(image){lv_obj_delete(image);image=NULL;}
    if(mode_label){lv_obj_delete(mode_label);mode_label=NULL;}
    lv_image_cache_drop(&picture);free(front);free(back);front=back=NULL;
    picture=(lv_image_dsc_t){0};inflight=false;atomic_store(&ready,false);
}
bool fluidbox_open(lv_obj_t *parent){
    fluidbox_close();
    front=heap_caps_calloc(LCD_H_RES*LCD_V_RES,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    back=heap_caps_calloc(LCD_H_RES*LCD_V_RES,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    exited=xSemaphoreCreateCounting(2,0);
    if(!front||!back||!exited){fluidbox_close();return false;}
    if(!initialized){sim_init();initialized=true;}else sim_reset();
    render_init();atomic_store(&mode,RENDER_PARTICLES);render_set_mode(RENDER_PARTICLES);
    sim_forces_t f=current_forces();sim_step(.001f,&f);
    frames=0;atomic_store(&steps,0);atomic_store(&render_us,0);atomic_store(&sim_us,0);
    atomic_store(&sim_stack,0);atomic_store(&render_stack,0);
    atomic_store(&stop,false);atomic_store(&awake,true);atomic_store(&reset,false);
    if(xTaskCreatePinnedToCore(solve,"fluid_sim",6144,NULL,2,&solver,1)!=pdPASS){solver=NULL;fluidbox_close();return false;}
    if(xTaskCreatePinnedToCore(draw,"fluid_draw",4096,NULL,2,&renderer,0)!=pdPASS){renderer=NULL;fluidbox_close();return false;}
    image=lv_image_create(parent);lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(image,0,0);
    picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
        .w=LCD_H_RES,.h=LCD_V_RES,.stride=LCD_H_RES*2},.data_size=LCD_H_RES*LCD_V_RES*2,.data=(uint8_t*)front};
    lv_image_set_src(image,&picture);
    mode_label=lv_label_create(parent);lv_obj_set_pos(mode_label,106,76);lv_obj_set_width(mode_label,148);
    lv_obj_set_style_text_font(mode_label,&font14,0);lv_obj_set_style_text_color(mode_label,lv_color_hex(0xAABACB),0);
    lv_obj_set_style_text_align(mode_label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(mode_label,names[0]);
    inflight=true;xTaskNotifyGive(renderer);return true;
}
void fluidbox_tick(bool running){
    if(!image)return;
    atomic_store(&awake,running);
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    if(!input.primed||now-input_at>=250){
        sim_forces_t fallback={.gravity={0,GRAVITY_GAIN*GRAVITY_MPS2*PX_PER_METER,0}};
        taskENTER_CRITICAL(&force_lock);forces=fallback;taskEXIT_CRITICAL(&force_lock);
    }
    if(!running)return;
    if(atomic_exchange(&ready,false)){
        uint16_t *swap=front;front=back;back=swap;picture.data=(uint8_t*)front;
        lv_image_cache_drop(&picture);lv_image_set_src(image,&picture);lv_obj_invalidate(image);
        frames++;inflight=false;
    }
    if(!inflight){inflight=true;xTaskNotifyGive(renderer);}
}
void fluidbox_info(char *out,size_t size){
    sim_forces_t f=current_forces();
    snprintf(out,size,"{\"active\":%s,\"running\":%s,\"mode\":%d,\"particles\":%d,\"frames\":%lu,\"steps\":%u,\"render_us\":%u,\"sim_us\":%u,\"sim_stack_free\":%u,\"render_stack_free\":%u,\"input_fresh\":%s,\"force\":[%.0f,%.0f,%.0f]}",
        image?"true":"false",image&&atomic_load(&awake)?"true":"false",atomic_load(&mode),PARTICLE_COUNT,
        (unsigned long)frames,atomic_load(&steps),atomic_load(&render_us),atomic_load(&sim_us),
        atomic_load(&sim_stack),atomic_load(&render_stack),input.primed&&(uint32_t)(esp_timer_get_time()/1000)-input_at<250?"true":"false",
        (double)f.gravity[0],(double)f.gravity[1],(double)f.gravity[2]);
}
