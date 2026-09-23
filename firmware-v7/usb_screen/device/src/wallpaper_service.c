#include "wallpaper_service.h"
#include "wallpaper_store.h"
#include "wallpaper_html.h"
#include "management.h"
#include "ui/badge_ui.h"
#include "ui/ui_transition_cache.h"
#include "protocol.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const esp_partition_t *partition;
static uint32_t flash_capacity;
static SemaphoreHandle_t lock;
static wall_record_t record={.slot=-1},library[WALL_LIBRARY_COUNT];
static lv_image_dsc_t thumbs[WALL_LIBRARY_COUNT];
static uint32_t thumb_used[WALL_LIBRARY_COUNT],thumb_clock;
static bool thumb_loading;
static int thumb_ready=-1;
static uint8_t *thumb_result;
static uint32_t thumb_result_generation;
static uint8_t *pending_thumb;
static int pending_thumb_slot=-1,pending_id;
static bool apply_pending,operation;
static uint8_t *thumbnail(const uint8_t *pixels){
    /* All image pixels stay in external PSRAM, including small previews. */
    uint16_t *out=heap_caps_malloc(180u*180u*2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(out){const uint16_t *in=(const uint16_t*)pixels;for(int y=0;y<180;y++)for(int x=0;x<180;x++)out[y*180+x]=in[(y*2)*360+x*2];}
    return (uint8_t*)out;
}
static void set_thumb(int slot,uint8_t *pixels){
    thumbs[slot]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=180,.h=180,.stride=360},.data_size=64800,.data=pixels};
}
static void publish_library(void){
    int ids[WALL_LIBRARY_COUNT],n=0;const lv_image_dsc_t *images[WALL_LIBRARY_COUNT];
    for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(library[i].slot>=0){ids[n]=i+2;images[n++]=&thumbs[i];}
    badge_ui_library(ids,images,n);
    unsigned free_slots=0;for(int i=0;i<WALL_LIBRARY_COUNT;i++)free_slots+=library[i].slot<0;
    badge_ui_storage(flash_capacity,free_slots*WALL_BYTES,free_slots,true);
}
int wallpaper_catalog(int ids[31]){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return -1;
    int n=0;for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(library[i].slot>=0)ids[n++]=i+2;
    xSemaphoreGive(lock);return n;
}
static struct {int owner;uint32_t session,size,crc,offset,generation;int64_t touched;uint8_t *pixels;} upload;
static uint8_t *pending,*display_pixels;
static bool changed,storage_ok,http_ok,hotspot;
static lv_image_dsc_t picture;
static char ap_name[24],ap_password[13],notice[80];
static int phase,last_error;
static uint32_t received;
static QueueHandle_t completed;
typedef struct {uint32_t session;int result;} finish_result_t;
static bool finishing;
static httpd_handle_t server;
bool wall_read(uint32_t offset,void *data,size_t size){return partition&&esp_partition_read(partition,offset,data,size)==ESP_OK;}
bool wall_write(uint32_t offset,const void *data,size_t size){return partition&&esp_partition_write(partition,offset,data,size)==ESP_OK;}
bool wall_erase(uint32_t offset,size_t size){return partition&&esp_partition_erase_range(partition,offset,size)==ESP_OK;}
bool wallpaper_resource_info(uint32_t *generation,uint32_t *size,uint32_t *crc){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return false;
    *generation=record.generation;*size=record.size;*crc=record.crc;xSemaphoreGive(lock);return true;
}
static void clear_upload(void){free(upload.pixels);memset(&upload,0,sizeof(upload));}
bool wallpaper_busy(void){
    if(!lock)return false;
    if(xSemaphoreTake(lock,0)!=pdTRUE)return true;
    bool busy=upload.owner||changed||finishing||operation;xSemaphoreGive(lock);return busy;
}
void wallpaper_status(int *p,int *error,uint32_t *bytes){
    *p=0;*error=0;*bytes=0;if(!lock)return;
    if(xSemaphoreTake(lock,0)!=pdTRUE){*p=-1;return;} /* Snapshot temporarily busy; do not invent a commit state. */
    *p=phase;*error=last_error;*bytes=received;xSemaphoreGive(lock);
}
int wallpaper_begin(int owner,uint32_t size,uint32_t crc,uint32_t *session){
    if(!lock||!storage_ok)return 3;
    if((size&&size!=WALL_BYTES)||(!size&&crc))return 2;
    if(xSemaphoreTake(lock,owner==2?pdMS_TO_TICKS(100):0)!=pdTRUE)return 1;
    if(upload.owner&&esp_timer_get_time()-upload.touched>30000000)clear_upload();
    int result=upload.owner||changed||finishing||operation?1:0;
    if(!result&&size){bool free_slot=false;for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(library[i].slot<0)free_slot=true;if(!free_slot)result=7;}
    if(!result){
        uint8_t *data=size?heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
        if(size&&!data)result=3;
        else {
            uint32_t id;do{id=esp_random();}while(!id);
            upload.owner=owner;upload.session=id;upload.size=size;upload.crc=crc;upload.pixels=data;upload.touched=esp_timer_get_time();*session=id;
            phase=1;last_error=0;received=0;
        }
    }
    xSemaphoreGive(lock);return result;
}
/* HTTP runs on a worker: wait briefly for UI snapshots; USB stays nonblocking. */
int wallpaper_chunk(int owner,uint32_t session,uint32_t offset,const uint8_t *data,size_t n){
    if(!lock||xSemaphoreTake(lock,owner==2?pdMS_TO_TICKS(100):0)!=pdTRUE)return 1;
    int result=4;
    if(upload.owner==owner&&upload.session==session&&upload.offset==offset&&offset<=upload.size&&n<=upload.size-offset&&n){
        if(esp_timer_get_time()-upload.touched>30000000){clear_upload();phase=7;last_error=4;}
        else {memcpy(upload.pixels+offset,data,n);upload.offset+=n;received=upload.offset;upload.touched=esp_timer_get_time();result=0;}
    }else if(upload.owner==owner&&upload.session==session){
        clear_upload();phase=5;last_error=4;
    }
    xSemaphoreGive(lock);return result;
}
int wallpaper_finish(int owner,uint32_t session){
    if(!lock||xSemaphoreTake(lock,owner==2?pdMS_TO_TICKS(100):0)!=pdTRUE)return 1;
    int result=4;
    if(upload.owner==owner&&upload.session==session){
        phase=2;
        if(owner==2&&!management_generation_valid(upload.generation))result=6;
        else if(esp_timer_get_time()-upload.touched>30000000)result=4;
        else if(upload.offset!=upload.size||wall_crc(upload.pixels,upload.size)!=upload.crc)result=2;
        else {
            phase=3;
            /* ponytail: one upload owns this lock through commit; UI uses try-lock,
             * other writers get busy. A storage worker would be needed for parallel writes. */
            int slot=-1;uint8_t *small=upload.size?thumbnail(upload.pixels):NULL;
            if(upload.size&&(!small||!wall_library_add(library,upload.pixels,upload.crc,&slot))){free(small);result=3;}
            else {
                pending_thumb=small;pending_thumb_slot=slot;pending_id=slot<0?0:slot+2;
                pending=upload.pixels;upload.pixels=NULL;apply_pending=true;changed=true;result=0;
                record=(wall_record_t){slot,record.generation+1,upload.size,upload.crc};
            }
        }
        if(result){phase=result==4?7:5;last_error=result;}
        clear_upload();
    }
    xSemaphoreGive(lock);return result;
}
void wallpaper_cancel(int owner,uint32_t session){
    if(lock&&xSemaphoreTake(lock,0)==pdTRUE){if(upload.owner==owner&&(!session||upload.session==session)){clear_upload();phase=6;}xSemaphoreGive(lock);}
}
static void finish_worker(void *arg){
    finish_result_t result={.session=(uint32_t)(uintptr_t)arg};result.result=wallpaper_finish(1,result.session);
    xQueueOverwrite(completed,&result);vTaskDelete(NULL);
}
int wallpaper_finish_async(uint32_t session){
    if(!completed||!lock)return 3;
    if(xSemaphoreTake(lock,0)!=pdTRUE)return 1;
    int result=finishing?1:upload.owner!=1||upload.session!=session?4:0;
    if(!result)finishing=true;
    xSemaphoreGive(lock);
    if(!result&&xTaskCreate(finish_worker,"wall_commit",4096,(void*)(uintptr_t)session,4,NULL)!=pdPASS){
        xSemaphoreTake(lock,portMAX_DELAY);finishing=false;xSemaphoreGive(lock);wallpaper_cancel(1,session);result=3;
    }
    return result;
}
bool wallpaper_finish_result(uint32_t *session,int *result){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return false;
    finish_result_t value;bool ready=!changed&&completed&&xQueueReceive(completed,&value,0)==pdTRUE;
    if(ready){*session=value.session;*result=value.result;finishing=false;}xSemaphoreGive(lock);return ready;
}
void wallpaper_stop_hotspot(void){
    if(wifi_wallpaper_hotspot_active())wifi_wallpaper_hotspot(false,ap_name,ap_password);
}
static void hotspot_toggle(void){
    if(wallpaper_busy()){snprintf(notice,sizeof(notice),"上传中，请稍后切换");return;}
    hotspot=wifi_wallpaper_hotspot_active();
    ap_password[0]=0;
    esp_err_t rc=wifi_wallpaper_hotspot(!hotspot,ap_name,ap_password);
    if(rc==ESP_OK){hotspot=!hotspot;snprintf(notice,sizeof(notice),"%s",hotspot?"手机连接热点后扫码上传":"直连热点已关闭");}
    else snprintf(notice,sizeof(notice),"热点切换失败，请重试");
}
void wallpaper_info(char *json,size_t n){
    hotspot=wifi_wallpaper_hotspot_active();
    char ip[24];wifi_wallpaper_address(ip,sizeof(ip));
    snprintf(json,n,"{\"available\":%s,\"http\":%s,\"ip\":\"%s\",\"hotspot\":%s,\"ssid\":\"%s\",\"password\":\"\"}",
        storage_ok?"true":"false",http_ok?"true":"false",ip,hotspot?"true":"false",ap_name);
}
static void headers(httpd_req_t *req){
    httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob: data:; connect-src 'self'; frame-ancestors 'none'");
}
static esp_err_t web_page(httpd_req_t *req){
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob: data:; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(req,(const char*)wallpaper_html,sizeof(wallpaper_html)-1);
}
static esp_err_t response(httpd_req_t *req,int result){
    headers(req);
    const char *status=result==0?"200 OK":(result==1||result==7)?"409 Conflict":result==3?"500 Internal Server Error":result==4?"408 Request Timeout":result==6?"403 Forbidden":"400 Bad Request";
    httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char text[40];snprintf(text,sizeof(text),"{\"result\":%d}",result);return httpd_resp_sendstr(req,text);
}
static bool web_request(httpd_req_t *req,uint32_t *generation){
    char origin[96],host[72],expected[80];
    /* Requests from a browser must come from this origin. Native GATT/USB and
     * explicit header-based HTTP clients do not depend on browser cookies. */
    if(httpd_req_get_hdr_value_len(req,"Origin")){
        if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK||
           httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK)return false;
        snprintf(expected,sizeof(expected),"http://%s",host);if(strcmp(expected,origin))return false;
    }
    return management_begin_request(generation);
}
static bool octet_stream(httpd_req_t *req){
    char type[40];return httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))==ESP_OK&&!strcmp(type,"application/octet-stream");
}
static bool receive_body(httpd_req_t *req,uint8_t *data,size_t n){
    size_t used=0;int64_t start=esp_timer_get_time();
    while(used<n){if(esp_timer_get_time()-start>5000000)return false;int got=httpd_req_recv(req,(char*)data+used,n-used);if(got<=0)return false;used+=got;}return true;
}
static esp_err_t web_status(httpd_req_t *req){
    uint32_t generation;if(!web_request(req,&generation)){response(req,6);return ESP_FAIL;}
    char *json=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!json){response(req,3);return ESP_OK;}
    if(!management_json(json,8192)){free(json);return response(req,1);}
    if(!management_generation_valid(generation)){free(json);response(req,6);return ESP_FAIL;}
    headers(req);httpd_resp_set_type(req,"application/json");esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
static esp_err_t web_control(httpd_req_t *req){
    uint32_t generation,id=0;if(!web_request(req,&generation)){response(req,6);return ESP_FAIL;}
    if(!octet_stream(req)||req->content_len<2||req->content_len>MG_MAX_COMMAND){response(req,2);return ESP_FAIL;}
    uint8_t command[MG_MAX_COMMAND];if(!receive_body(req,command,req->content_len)){memset(command,0,sizeof(command));response(req,4);return ESP_FAIL;}
    int result=management_submit(command,req->content_len,generation,UINT16_MAX,&id);memset(command,0,sizeof(command));
    if(result!=MG_ACCEPTED)return response(req,result==MG_UNAUTHORIZED?6:result);
    headers(req);httpd_resp_set_type(req,"application/json");httpd_resp_set_status(req,"202 Accepted");
    char text[64];snprintf(text,sizeof(text),"{\"result\":5,\"id\":%lu}",(unsigned long)id);return httpd_resp_sendstr(req,text);
}
static esp_err_t web_cancel(httpd_req_t *req){
    uint32_t generation;if(!web_request(req,&generation)){response(req,6);return ESP_FAIL;}
    if(req->content_len){response(req,2);return ESP_FAIL;}
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return response(req,1);
    int result=0;
    if(upload.owner==2){clear_upload();phase=6;}
    else if(changed||phase==3||phase==4)result=1; /* a committed image cannot be cancelled */
    xSemaphoreGive(lock);return response(req,result);
}
typedef struct {httpd_req_t *req;uint32_t session,generation;} web_job_t;
static void web_receive(void *arg){
    web_job_t job=*(web_job_t*)arg;free(arg);httpd_req_t *req=job.req;
    uint8_t buffer[2048];uint32_t offset=0;int result=0;int64_t start=esp_timer_get_time();
    while(offset<(uint32_t)req->content_len){
        if(!management_generation_valid(job.generation)){result=6;break;}
        if(esp_timer_get_time()-start>30000000){result=4;break;}
        size_t left=req->content_len-offset,n=left<sizeof(buffer)?left:sizeof(buffer);
        int got=httpd_req_recv(req,(char*)buffer,n);if(got<=0){result=4;break;}
        result=wallpaper_chunk(2,job.session,offset,buffer,got);if(result)break;offset+=got;
    }
    if(!result&&!management_generation_valid(job.generation))result=6;
    if(!result)result=wallpaper_finish(2,job.session);
    if(result)wallpaper_cancel(2,job.session);
    response(req,result);
    bool unread=offset<(uint32_t)req->content_len;
    int fd=httpd_req_to_sockfd(req);
    httpd_req_async_handler_complete(req);
    if(unread)httpd_sess_trigger_close(server,fd);
    vTaskDelete(NULL);
}
static esp_err_t web_upload(httpd_req_t *req){
    uint32_t generation;if(!web_request(req,&generation)){response(req,6);return ESP_FAIL;}
    bool reset=!strcmp(req->uri,"/reset");
    if(!octet_stream(req)||req->content_len!=(reset?0:WALL_BYTES)){response(req,2);return ESP_FAIL;}
    uint32_t crc=0;
    if(!reset){
        char check[9];if(httpd_req_get_hdr_value_str(req,"X-JX-CRC",check,sizeof(check))!=ESP_OK||strlen(check)!=8){response(req,2);return ESP_FAIL;}
        for(int i=0;i<8;i++){int digit=mg_unhex(check[i]);if(digit<0){response(req,2);return ESP_FAIL;}crc=(crc<<4)|digit;}
    }
    web_job_t *job=calloc(1,sizeof(*job));if(!job){response(req,3);return ESP_FAIL;}
    int result=wallpaper_begin(2,req->content_len,crc,&job->session);
    if(result){free(job);response(req,result);return ESP_FAIL;}
    job->generation=generation;
    if(xSemaphoreTake(lock,pdMS_TO_TICKS(100))!=pdTRUE){wallpaper_cancel(2,job->session);free(job);response(req,1);return ESP_FAIL;}
    if(upload.owner!=2||upload.session!=job->session){xSemaphoreGive(lock);free(job);response(req,6);return ESP_FAIL;}
    upload.generation=generation;xSemaphoreGive(lock);
    if(httpd_req_async_handler_begin(req,&job->req)!=ESP_OK){wallpaper_cancel(2,job->session);free(job);response(req,3);return ESP_FAIL;}
    if(xTaskCreate(web_receive,"wall_http",6144,job,4,NULL)!=pdPASS){
        wallpaper_cancel(2,job->session);response(job->req,3);httpd_req_async_handler_complete(job->req);free(job);return ESP_FAIL;
    }
    return ESP_OK;
}
typedef struct {int id;bool remove,selected;} library_job_t;
static void library_worker(void *arg){
    library_job_t job=*(library_job_t*)arg;free(arg);
    uint8_t *pixels=NULL;int result=0;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(job.remove){
        if(!wall_library_delete(library,job.id-2))result=3;
        else {pending_thumb_slot=job.id-2;pending_thumb=NULL;apply_pending=job.selected;pending_id=0;}
    }else {
        pixels=heap_caps_malloc(WALL_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!pixels||!wall_library_read(&library[job.id-2],pixels))result=3;
        else {apply_pending=true;pending_id=job.id;}
    }
    if(!result){pending=pixels;changed=true;record.generation++;if(apply_pending){record.slot=pending_id-2;record.size=pixels?WALL_BYTES:0;record.crc=pixels?library[job.id-2].crc:0;}}
    else {free(pixels);phase=5;last_error=result;}
    operation=false;xSemaphoreGive(lock);vTaskDelete(NULL);
}
int wallpaper_select(int id,bool remove){
    if(id<0||id>=WALL_LIBRARY_COUNT+2||(remove&&id<2))return MG_INVALID;
    if(wallpaper_busy())return MG_BUSY;
    if(id<2){bool ok=badge_ui_select_wallpaper(id);badge_ui_apply_result(ok);return ok?MG_OK:MG_FAILED;}
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return MG_BUSY;
    if(library[id-2].slot<0){xSemaphoreGive(lock);return MG_INVALID;}
    library_job_t *job=malloc(sizeof(*job));if(!job){xSemaphoreGive(lock);return MG_FAILED;}
    *job=(library_job_t){id,remove,badge_ui_selected_wallpaper()==id};operation=true;phase=3;last_error=0;
    xSemaphoreGive(lock);
    if(xTaskCreate(library_worker,"wall_library",4096,job,4,NULL)!=pdPASS){free(job);xSemaphoreTake(lock,portMAX_DELAY);operation=false;phase=5;last_error=3;xSemaphoreGive(lock);return MG_FAILED;}
    return MG_OK;
}
void wallpaper_service_init(void){
    if(esp_flash_get_size(NULL,&flash_capacity)!=ESP_OK)flash_capacity=0;
    badge_ui_storage(flash_capacity,0,0,false);
    snprintf(ap_name,sizeof(ap_name),"CABadge-%04lx",(unsigned long)(esp_random()&0xffff));ap_password[0]=0;
    lock=xSemaphoreCreateMutex();completed=xQueueCreate(1,sizeof(finish_result_t));partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"wallpaper");
    if(lock&&partition&&partition->size>=WALL_LIBRARY_END){
        uint8_t *pixels=heap_caps_malloc(WALL_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(pixels){
            storage_ok=wall_library_scan(library)&&wall_library_migrate(library,pixels);
            if(storage_ok){
                for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(library[i].slot>=0){
                    if(library[i].generation>record.generation)record.generation=library[i].generation;
                    set_thumb(i,NULL);
                }
                publish_library();int id=badge_ui_selected_wallpaper();
                if(id>=2&&id<WALL_LIBRARY_COUNT+2&&wall_library_read(&library[id-2],pixels)){
                    pending=pixels;pixels=NULL;pending_id=id;apply_pending=true;changed=true;
                    record.size=WALL_BYTES;record.crc=library[id-2].crc;
                }else if(id<0||id>=2)badge_ui_select_wallpaper(0);
            }
            free(pixels);
        }
    }
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_open_sockets=3;cfg.lru_purge_enable=true;cfg.recv_wait_timeout=5;cfg.send_wait_timeout=5;
    if(httpd_start(&server,&cfg)==ESP_OK){
        httpd_uri_t home={.uri="/",.method=HTTP_GET,.handler=web_page};
        httpd_uri_t send={.uri="/upload",.method=HTTP_POST,.handler=web_upload};
        httpd_uri_t reset={.uri="/reset",.method=HTTP_POST,.handler=web_upload};
        httpd_uri_t status={.uri="/status",.method=HTTP_GET,.handler=web_status};
        httpd_uri_t control={.uri="/control",.method=HTTP_POST,.handler=web_control};
        httpd_uri_t cancel={.uri="/cancel",.method=HTTP_POST,.handler=web_cancel};
        http_ok=httpd_register_uri_handler(server,&home)==ESP_OK&&httpd_register_uri_handler(server,&send)==ESP_OK&&httpd_register_uri_handler(server,&reset)==ESP_OK&&
            httpd_register_uri_handler(server,&status)==ESP_OK&&httpd_register_uri_handler(server,&control)==ESP_OK&&httpd_register_uri_handler(server,&cancel)==ESP_OK;
    }
    badge_wallpaper_bind(hotspot_toggle);
}
/* Five-entry LRU, with the three visible cards pinned. Worker never touches LVGL. */
static void thumb_worker(void *arg){
    int slot=(int)(intptr_t)arg;uint8_t *pixels=NULL,*rows=NULL;wall_record_t r;
    xSemaphoreTake(lock,portMAX_DELAY);r=library[slot];xSemaphoreGive(lock);
    pixels=heap_caps_malloc(64800,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    rows=heap_caps_malloc(1440,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint32_t crc=~0u;bool ok=pixels&&rows&&r.slot>=0;
    for(unsigned y=0;ok&&y<180;y++){
        ok=wall_read(WALL_LIBRARY_BASE+(uint32_t)r.slot*WALL_LIBRARY_SLOT+WALL_DATA+y*1440,rows,1440);
        if(ok){crc=jx_crc(crc,rows,1440);for(unsigned x=0;x<180;x++)memcpy(pixels+(y*180+x)*2,rows+x*4,2);}
    }
    free(rows);ok=ok&&~crc==r.crc;
    xSemaphoreTake(lock,portMAX_DELAY);
    ok=ok&&library[slot].slot==r.slot&&library[slot].generation==r.generation;
    if(ok){thumb_result=pixels;thumb_ready=slot;thumb_result_generation=r.generation;}else free(pixels);
    thumb_loading=false;xSemaphoreGive(lock);vTaskDelete(NULL);
}
static void thumbnail_poll(void){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return;
    if(thumb_ready>=0&&(library[thumb_ready].slot<0||library[thumb_ready].generation!=thumb_result_generation)){
        free(thumb_result);thumb_result=NULL;thumb_ready=-1;
    }
    if(thumb_ready>=0){
        int slot=thumb_ready;const uint8_t *old=thumbs[slot].data;lv_image_cache_drop(&thumbs[slot]);
        set_thumb(slot,thumb_result);thumb_result=NULL;thumb_ready=-1;thumb_used[slot]=++thumb_clock;
        badge_ui_thumbnails_changed();free((void*)old);
    }
    int ids[3];unsigned n=badge_ui_thumbnail_window(ids);bool pinned[WALL_LIBRARY_COUNT]={0};
    for(unsigned j=0;j<n;j++){int slot=ids[j]-2;pinned[slot]=true;thumb_used[slot]=++thumb_clock;}
    unsigned count=0;for(int i=0;i<WALL_LIBRARY_COUNT;i++)count+=thumbs[i].data!=NULL;
    unsigned limit=ui_memory_pressure_get()==UI_MEMORY_NORMAL?5:n;
    while(count>limit){
        int victim=-1;for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(thumbs[i].data&&!pinned[i]&&(victim<0||thumb_used[i]<thumb_used[victim]))victim=i;
        if(victim<0)break;
        const uint8_t *old=thumbs[victim].data;lv_image_cache_drop(&thumbs[victim]);set_thumb(victim,NULL);
        badge_ui_thumbnails_changed();free((void*)old);count--;
    }
    bool idle=!badge_ui_transition_active()&&!lv_anim_count_running();
    for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))if(lv_indev_get_state(in)==LV_INDEV_STATE_PRESSED)idle=false;
    if(idle&&!thumb_loading&&!operation&&!changed&&ui_memory_pressure_get()==UI_MEMORY_NORMAL&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>2*1024*1024&&heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>32*1024){
        for(unsigned j=0;j<n;j++)if(!thumbs[ids[j]-2].data){
            thumb_loading=true;
            if(xTaskCreate(thumb_worker,"wall_thumb",4096,(void*)(intptr_t)(ids[j]-2),2,NULL)!=pdPASS)thumb_loading=false;
            break;
        }
    }
    xSemaphoreGive(lock);
}
void wallpaper_service_poll(void){
    thumbnail_poll();
    if(lock&&xSemaphoreTake(lock,0)==pdTRUE){
        if(upload.owner&&esp_timer_get_time()-upload.touched>30000000){clear_upload();phase=7;last_error=4;}
        if(changed){
            const uint8_t *old_thumb=NULL;
            if(pending_thumb_slot>=0){
                int slot=pending_thumb_slot;old_thumb=thumbs[slot].data;lv_image_cache_drop(&thumbs[slot]);
                set_thumb(slot,pending_thumb);pending_thumb=NULL;pending_thumb_slot=-1;
            }
            publish_library();free((void*)old_thumb);
            if(apply_pending){
                lv_image_cache_drop(&picture);uint8_t *old=display_pixels;display_pixels=pending;pending=NULL;
                picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=WALL_BYTES,.data=display_pixels};
                badge_ui_loaded_photo(display_pixels?&picture:NULL,pending_id,true);free(old);
                badge_ui_sleep(false);
            }
            apply_pending=false;changed=false;phase=4;last_error=0;
            snprintf(notice,sizeof(notice),"壁纸已更新");
        }
        xSemaphoreGive(lock);
    }
    static int64_t last;int64_t now=esp_timer_get_time();
    if(now-last>1000000){
        hotspot=wifi_wallpaper_hotspot_active();
        last=now;char ip[24],url[128];wifi_wallpaper_address(ip,sizeof(ip));
        snprintf(url,sizeof(url),"http://%s/",hotspot?"192.168.4.1":ip);
        badge_wallpaper_update((*ip||hotspot)&&http_ok?url:"",hotspot,ap_name,ap_password,
            !storage_ok?"壁纸存储不可用，请检查固件":!http_ok?"上传服务启动失败":notice);
    }
}
