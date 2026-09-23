/* Host substitutes for ESP I/O. The test includes the production services. */
#ifndef SERVICE_TEST_SHIM_H
#define SERVICE_TEST_SHIM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_IMAGE_HEADER_MAGIC 25
#define LV_COLOR_FORMAT_RGB565 1
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
typedef struct {bool held;} *SemaphoreHandle_t;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return calloc(1,sizeof(struct {bool held;}));}
static SemaphoreHandle_t transient_lock,receive_lock;
static size_t contend_offset;
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned ticks){
    if(s&&s==transient_lock){transient_lock=NULL;if(!ticks)return 0;}
    if(!s||s->held)return 0;s->held=true;return 1;
}
static void xSemaphoreGive(SemaphoreHandle_t s){s->held=false;}
typedef struct {size_t size;bool full;uint8_t data[128];} *QueueHandle_t;
static QueueHandle_t xQueueCreate(int n,size_t size){(void)n;QueueHandle_t q=calloc(1,sizeof(*q));q->size=size;return q;}
static int xQueueOverwrite(QueueHandle_t q,const void *data){memcpy(q->data,data,q->size);q->full=true;return 1;}
static int xQueueReceive(QueueHandle_t q,void *data,unsigned ticks){(void)ticks;if(!q->full)return 0;memcpy(data,q->data,q->size);q->full=false;return 1;}
static void (*queued_task)(void*);static void *queued_arg;
static bool task_failure;
static int xTaskCreate(void(*task)(void*),const char *name,int stack,void *arg,int priority,void *handle){
    (void)name;(void)stack;(void)priority;(void)handle;if(task_failure)return 0;queued_task=task;queued_arg=arg;return 1;
}
static void vTaskDelete(void *unused){(void)unused;}
static void run_task(void){void (*task)(void*)=queued_task;void *arg=queued_arg;queued_task=NULL;queued_arg=NULL;if(task)task(arg);}
static int64_t clock_us=1000000;
static int64_t esp_timer_get_time(void){return clock_us;}
static uint32_t esp_random(void){static uint32_t n=4;return ++n;}
static void esp_fill_random(void *out,size_t n){for(size_t i=0;i<n;i++)((uint8_t*)out)[i]=esp_random();}
static bool allocation_failure;
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return allocation_failure?NULL:malloc(n);}
typedef struct {size_t size;} esp_partition_t;
static uint8_t mock_flash[0x8f0000];static esp_partition_t mock_partition={sizeof(mock_flash)};
static bool flash_failure;
static int esp_flash_get_size(void *chip,uint32_t *size){(void)chip;*size=16u*1024*1024;return ESP_OK;}
static uint32_t shown_storage_total,shown_storage_free;static unsigned shown_storage_slots;
static void badge_ui_storage(uint32_t total,uint32_t available,unsigned slots,bool valid){shown_storage_total=total;shown_storage_free=valid?available:0;shown_storage_slots=valid?slots:0;}
static const esp_partition_t *esp_partition_find_first(int type,int sub,const char *name){(void)type;(void)sub;(void)name;return &mock_partition;}
static int esp_partition_read(const esp_partition_t *p,size_t off,void *data,size_t n){if(off+n>p->size)return -1;memcpy(data,mock_flash+off,n);return 0;}
static int esp_partition_write(const esp_partition_t *p,size_t off,const void *data,size_t n){if(flash_failure||off+n>p->size)return -1;for(size_t i=0;i<n;i++)mock_flash[off+i]&=((const uint8_t*)data)[i];return 0;}
static int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n){if(flash_failure||off+n>p->size)return -1;memset(mock_flash+off,255,n);return 0;}
typedef struct {struct {int magic,cf,w,h,stride;} header;size_t data_size;const uint8_t *data;} lv_image_dsc_t;
static const lv_image_dsc_t badge_wallpaper={0};static const lv_image_dsc_t *shown;
static void lv_image_cache_drop(void *p){(void)p;}
typedef struct {int brightness,wallpaper_index;bool reduced_motion;int battery_mv,wifi_connected,ble_connected,wifi_rssi;bool live,wifi_enabled;char wifi_ssid[33];} badge_state_t;
typedef struct {char ssid[33];int rssi;unsigned security;bool saved;} badge_wifi_ap_t;
typedef struct {char name[33];uint8_t address[6],address_type;int rssi;bool connectable;} badge_ble_device_t;
typedef struct {bool enabled,advertising,scanning,connecting,inbound,outbound;int error,count;char inbound_address[18],outbound_address[18],outbound_name[33];badge_ble_device_t devices[12];} badge_ble_info_t;
static int selected_wallpaper;
static int badge_ui_selected_wallpaper(void){return selected_wallpaper;}
static bool badge_ui_select_wallpaper(int id){selected_wallpaper=id;return true;}
static void badge_ui_apply_result(bool ok){(void)ok;}
static void badge_ui_library(const int *ids,const lv_image_dsc_t *const *images,int n){(void)ids;(void)images;(void)n;}
static void badge_ui_loaded_photo(const lv_image_dsc_t *p,int id,bool apply){shown=p;if(apply)selected_wallpaper=id;}
static bool asleep,mock_hotspot;static int setting_calls,radio_commands;
#define UI_VERSION "7.0.0-preview"
static void badge_ui_set_photo(const lv_image_dsc_t *p){shown=p;}
static void badge_ui_restore_photo(const lv_image_dsc_t *p){shown=p;}
static void badge_ui_sleep(bool value){asleep=value;}
static bool badge_ui_is_asleep(void){return asleep;}
static void badge_ui_settings(int value,bool reduced){(void)value;(void)reduced;setting_calls++;}
static void badge_management_bind(void(*cb)(void)){(void)cb;}
static void badge_wallpaper_bind(void(*cb)(void)){(void)cb;}
static void badge_management_update(bool active,const char *code,unsigned remaining){(void)active;(void)code;(void)remaining;}
static void badge_wallpaper_update(const char *url,bool on,const char *name,const char *pass,const char *message){(void)url;(void)on;(void)name;(void)pass;(void)message;}
static const unsigned char wallpaper_html[]="<html></html>";
#define HTTP_GET 0
#define HTTP_POST 1
typedef void *httpd_handle_t;
typedef struct {int stack_size,max_open_sockets,recv_wait_timeout,send_wait_timeout;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct {const char *uri;size_t content_len,offset;const uint8_t *body;const char *key,*crc,*type,*origin,*host;char status[32],response[8192];int fail_after;bool closed;int security_headers;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t(*handler)(httpd_req_t*);} httpd_uri_t;
static httpd_uri_t registered[8];static int registered_count;
static int httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){(void)cfg;*out=(void*)1;return 0;}
static int httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *route){(void)s;registered[registered_count++]=*route;return 0;}
static const char *header(httpd_req_t *r,const char *key){
    if(!strcmp(key,"X-JX-Key"))return r->key;if(!strcmp(key,"X-JX-CRC"))return r->crc;
    if(!strcmp(key,"Content-Type"))return r->type;if(!strcmp(key,"Origin"))return r->origin;if(!strcmp(key,"Host"))return r->host;return NULL;
}
static size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *key){const char *p=header(r,key);return p?strlen(p):0;}
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n){const char *p=header(r,key);if(!p||strlen(p)>=n)return -1;strcpy(out,p);return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)key;(void)value;r->security_headers++;return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *type){(void)r;(void)type;return 0;}
static int httpd_resp_set_status(httpd_req_t *r,const char *status){snprintf(r->status,sizeof(r->status),"%s",status);return 0;}
static int httpd_resp_send(httpd_req_t *r,const char *data,size_t n){snprintf(r->response,sizeof(r->response),"%.*s",(int)n,data);return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *data){return httpd_resp_send(r,data,strlen(data));}
static int httpd_req_recv(httpd_req_t *r,char *out,size_t n){
    if(r->fail_after>=0&&(int)r->offset>=r->fail_after)return -1;
    if(n>r->content_len-r->offset)n=r->content_len-r->offset;memcpy(out,r->body+r->offset,n);r->offset+=n;if(receive_lock&&r->offset>=contend_offset){transient_lock=receive_lock;receive_lock=NULL;}return (int)n;
}
static int httpd_req_async_handler_begin(httpd_req_t *r,httpd_req_t **out){*out=r;return 0;}
static int httpd_req_async_handler_complete(httpd_req_t *r){(void)r;return 0;}
static int httpd_req_to_sockfd(httpd_req_t *r){(void)r;return 1;}
static int httpd_sess_trigger_close(httpd_handle_t s,int fd){(void)s;(void)fd;return 0;}
#endif
