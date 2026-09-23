#include "frame_trace.h"
#if CABADGE_DISPLAY_PERF
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/spi_master.h"
#include <stdatomic.h>
#include <string.h>
/* Bounded internal-RAM trace; GUI owns start/end/export, ISR only appends.
 * End is called only after all LCD completions have been consumed. */
static struct {uint32_t us,code,value;} records[640];
static atomic_bool active;
static atomic_uint used;
static uint32_t epoch,identifier;
static bool ready;
static bool invalid_seen;
void IRAM_ATTR frame_trace(unsigned code,uint32_t value){
    if(!atomic_load_explicit(&active,memory_order_relaxed))return;
    if(code==1)invalid_seen=false;
    if(code==22){if(invalid_seen)return;invalid_seen=true;}
    unsigned i=atomic_fetch_add_explicit(&used,1,memory_order_relaxed);
    if(i<640){records[i].us=(uint32_t)esp_timer_get_time()-epoch;records[i].code=code;records[i].value=value;}
}
void frame_trace_begin(uint32_t id){
    if(ready)return; /* Never overwrite an unexported transition. */
    identifier=id;epoch=(uint32_t)esp_timer_get_time();atomic_store(&used,0);atomic_store(&active,true);
}
void frame_trace_end(void){if(atomic_exchange(&active,false))ready=true;}
size_t frame_trace_take(void *out,size_t capacity){
    if(!ready)return 0;
    unsigned n=atomic_load(&used),count=n>640?640:n;
    uint32_t header[]={identifier,count,n-count};size_t bytes=sizeof(header)+count*sizeof(records[0]);
    if(bytes>capacity)return 0;
    memcpy(out,header,sizeof(header));memcpy((char*)out+sizeof(header),records,count*sizeof(records[0]));ready=false;return bytes;
}
void __real_lv_display_flush_ready(lv_display_t *d);
void IRAM_ATTR __wrap_lv_display_flush_ready(lv_display_t *d){frame_trace(6,0);__real_lv_display_flush_ready(d);}
/* Retain native timer callback order and period, only sandwich the callback. */
static struct {lv_timer_t *timer;lv_timer_cb_t callback;} timers[32];
static void traced_timer(lv_timer_t *timer){
    for(unsigned i=0;i<32;i++)if(timers[i].timer==timer){
        lv_timer_cb_t cb=timers[i].callback;frame_trace(15,(uint32_t)cb);cb(timer);frame_trace(16,(uint32_t)cb);return;
    }
}
lv_timer_t *__real_lv_timer_create(lv_timer_cb_t cb,uint32_t period,void *data);
lv_timer_t *__wrap_lv_timer_create(lv_timer_cb_t cb,uint32_t period,void *data){
    lv_timer_t *t=__real_lv_timer_create(cb,period,data);
    if(t&&cb)for(unsigned i=0;i<32;i++)if(!timers[i].timer){timers[i].timer=t;timers[i].callback=cb;lv_timer_set_cb(t,traced_timer);break;}
    return t;
}
void __real_lv_timer_delete(lv_timer_t *timer);
void __wrap_lv_timer_delete(lv_timer_t *timer){
    for(unsigned i=0;i<32;i++)if(timers[i].timer==timer){timers[i].timer=NULL;break;}
    __real_lv_timer_delete(timer);
}
/* This board has one SPI2 LCD device. Preserve its callbacks verbatim. */
static transaction_cb_t lcd_post;
static spi_device_handle_t lcd_spi;
static void IRAM_ATTR traced_post(spi_transaction_t *t){if(t->length>64)frame_trace(21,t->length/8);if(lcd_post)lcd_post(t);}
esp_err_t __real_spi_bus_add_device(spi_host_device_t h,const spi_device_interface_config_t *c,spi_device_handle_t *d);
esp_err_t __wrap_spi_bus_add_device(spi_host_device_t h,const spi_device_interface_config_t *c,spi_device_handle_t *d){
    if(h!=SPI2_HOST)return __real_spi_bus_add_device(h,c,d);
    spi_device_interface_config_t copy=*c;lcd_post=c->post_cb;copy.post_cb=traced_post;
    esp_err_t e=__real_spi_bus_add_device(h,&copy,d);if(e==ESP_OK)lcd_spi=*d;return e;
}
esp_err_t __real_spi_device_queue_trans(spi_device_handle_t d,spi_transaction_t *t,uint32_t wait);
esp_err_t __wrap_spi_device_queue_trans(spi_device_handle_t d,spi_transaction_t *t,uint32_t wait){
    if(d==lcd_spi)frame_trace(17,t->length/8);
    esp_err_t e=__real_spi_device_queue_trans(d,t,wait);
    if(d==lcd_spi)frame_trace(18,e);
    return e;
}
esp_err_t __real_spi_device_get_trans_result(spi_device_handle_t d,spi_transaction_t **t,uint32_t wait);
esp_err_t __wrap_spi_device_get_trans_result(spi_device_handle_t d,spi_transaction_t **t,uint32_t wait){
    if(d==lcd_spi)frame_trace(19,0);
    esp_err_t e=__real_spi_device_get_trans_result(d,t,wait);
    if(d==lcd_spi)frame_trace(20,e);
    return e;
}
#endif
