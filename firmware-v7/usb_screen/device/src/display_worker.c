#include "display_worker.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {lv_area_t area;const uint8_t *pixels;bool last;} display_job_t;
static QueueHandle_t jobs,results;
static void display_worker(void *unused){
    (void)unused;display_job_t job;
    for(;;){
        xQueueReceive(jobs,&job,portMAX_DELAY);
        physical_display_flush(&job.area,job.pixels);
        display_result_t result={.flush_us=physical_display_flush_us(),.copy_us=physical_display_copy_us(),
            .dma=physical_display_profile_result(),.error=physical_display_error(),.last=job.last};
        /* physical_display_flush drains every queued SPI transaction, including failure paths. */
        xQueueSend(results,&result,portMAX_DELAY);
    }
}
void display_worker_init(void){
    jobs=xQueueCreate(1,sizeof(display_job_t));results=xQueueCreate(1,sizeof(display_result_t));
    ESP_ERROR_CHECK(jobs&&results?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(display_worker,"badge_lcd",3072,NULL,3,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
void display_worker_submit(const lv_area_t *area,const uint8_t *pixels,bool last){
    display_job_t job={.area=*area,.pixels=pixels,.last=last};
    /* LVGL waits for the previous flush before submitting another. Overflow is a programming error. */
    BaseType_t sent=xQueueSend(jobs,&job,0);
    ESP_ERROR_CHECK(sent==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE);
}
display_result_t display_worker_wait(void){
    display_result_t result;
    BaseType_t received=xQueueReceive(results,&result,portMAX_DELAY);
    ESP_ERROR_CHECK(received==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE);
    return result;
}

bool display_worker_poll(display_result_t *result){return xQueueReceive(results,result,0)==pdTRUE;}
