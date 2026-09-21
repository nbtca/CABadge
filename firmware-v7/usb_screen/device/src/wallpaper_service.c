#include "wallpaper_service.h"
#include "wallpaper_store.h"
#include "wallpaper_html.h"
#include "management.h"
#include "ui/badge_ui.h"
#include "src/misc/cache/lv_cache.h"
#include "esp_partition.h"
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
static SemaphoreHandle_t lock;
static wall_record_t record={.slot=-1};
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
bool wallpaper_read_resource(uint32_t generation,uint32_t offset,void *data,size_t size){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return false;
    bool ok=!changed&&!finishing&&record.slot>=0&&record.generation==generation&&record.size==WALL_BYTES&&offset<=record.size&&size<=record.size-offset;
    if(ok)ok=wall_read(record.slot*WALL_SLOT+WALL_DATA+offset,data,size);
    xSemaphoreGive(lock);return ok;
}
static void clear_upload(void){free(upload.pixels);memset(&upload,0,sizeof(upload));}
bool wallpaper_busy(void){
    if(!lock)return false;
    if(xSemaphoreTake(lock,0)!=pdTRUE)return true;
    bool busy=upload.owner||changed||finishing;xSemaphoreGive(lock);return busy;
}
void wallpaper_status(int *p,int *error,uint32_t *bytes){
    *p=0;*error=0;*bytes=0;if(!lock)return;
    if(xSemaphoreTake(lock,0)!=pdTRUE){*p=-1;return;} /* Snapshot temporarily busy; do not invent a commit state. */
    *p=phase;*error=last_error;*bytes=received;xSemaphoreGive(lock);
}
int wallpaper_begin(int owner,uint32_t size,uint32_t crc,uint32_t *session){
    if(!lock||!storage_ok)return 3;
    if((size&&size!=WALL_BYTES)||(!size&&crc))return 2;
    if(xSemaphoreTake(lock,0)!=pdTRUE)return 1;
    if(upload.owner&&esp_timer_get_time()-upload.touched>30000000)clear_upload();
    int result=upload.owner||changed||finishing?1:0;
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
int wallpaper_chunk(int owner,uint32_t session,uint32_t offset,const uint8_t *data,size_t n){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return 1;
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
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return 1;
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
            if(!wall_commit(&record,upload.pixels,upload.size,upload.crc))result=3;
            else {pending=upload.pixels;upload.pixels=NULL;changed=true;result=0;}
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
    if(!hotspot){
        if(!management_active())management_toggle();
        snprintf(ap_password,sizeof(ap_password),"%08lx%04lx",(unsigned long)esp_random(),(unsigned long)(esp_random()&0xffff));
    }else management_close();
    esp_err_t rc=wifi_wallpaper_hotspot(!hotspot,ap_name,ap_password);
    if(rc==ESP_OK){hotspot=!hotspot;snprintf(notice,sizeof(notice),"%s",hotspot?"手机连接热点后扫码上传":"直连热点已关闭");}
    else snprintf(notice,sizeof(notice),"热点切换失败，请重试");
}
void wallpaper_info(char *json,size_t n){
    hotspot=wifi_wallpaper_hotspot_active();
    char ip[24],key[33];wifi_wallpaper_address(ip,sizeof(ip));management_key(key);
    snprintf(json,n,"{\"available\":%s,\"http\":%s,\"ip\":\"%s\",\"key\":\"%s\",\"hotspot\":%s,\"ssid\":\"%s\",\"password\":\"%s\",\"authorized\":%s,\"remaining\":%u}",
        storage_ok?"true":"false",http_ok?"true":"false",ip,key,hotspot?"true":"false",ap_name,hotspot?ap_password:"",*key?"true":"false",management_remaining());
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
    const char *status=result==0?"200 OK":result==1?"409 Conflict":result==3?"500 Internal Server Error":result==4?"408 Request Timeout":result==6?"403 Forbidden":"400 Bad Request";
    httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char text[40];snprintf(text,sizeof(text),"{\"result\":%d}",result);return httpd_resp_sendstr(req,text);
}
static bool web_auth(httpd_req_t *req,uint32_t *generation){
    char supplied[33],origin[96],host[72],expected[80];
    /* Requests from a browser must come from this origin. Native GATT/USB and
     * explicit header-based HTTP clients do not depend on browser cookies. */
    if(httpd_req_get_hdr_value_len(req,"Origin")){
        if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK||
           httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK)return false;
        snprintf(expected,sizeof(expected),"http://%s",host);if(strcmp(expected,origin))return false;
    }
    if(httpd_req_get_hdr_value_str(req,"X-JX-Key",supplied,sizeof(supplied))!=ESP_OK)return false;
    bool ok=management_http_auth(supplied,generation);memset(supplied,0,sizeof(supplied));return ok;
}
static bool octet_stream(httpd_req_t *req){
    char type[40];return httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))==ESP_OK&&!strcmp(type,"application/octet-stream");
}
static bool receive_body(httpd_req_t *req,uint8_t *data,size_t n){
    size_t used=0;int64_t start=esp_timer_get_time();
    while(used<n){if(esp_timer_get_time()-start>5000000)return false;int got=httpd_req_recv(req,(char*)data+used,n-used);if(got<=0)return false;used+=got;}return true;
}
static esp_err_t web_status(httpd_req_t *req){
    uint32_t generation;if(!web_auth(req,&generation)){response(req,6);return ESP_FAIL;}
    char *json=malloc(8192);if(!json){response(req,3);return ESP_OK;}
    if(!management_json(json,8192)){free(json);return response(req,1);}
    if(!management_generation_valid(generation)){free(json);response(req,6);return ESP_FAIL;}
    headers(req);httpd_resp_set_type(req,"application/json");esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
static esp_err_t web_control(httpd_req_t *req){
    uint32_t generation,id=0;if(!web_auth(req,&generation)){response(req,6);return ESP_FAIL;}
    if(!octet_stream(req)||req->content_len<2||req->content_len>MG_MAX_COMMAND){response(req,2);return ESP_FAIL;}
    uint8_t command[MG_MAX_COMMAND];if(!receive_body(req,command,req->content_len)){memset(command,0,sizeof(command));response(req,4);return ESP_FAIL;}
    int result=management_submit(command,req->content_len,generation,UINT16_MAX,&id);memset(command,0,sizeof(command));
    if(result!=MG_ACCEPTED)return response(req,result==MG_UNAUTHORIZED?6:result);
    headers(req);httpd_resp_set_type(req,"application/json");httpd_resp_set_status(req,"202 Accepted");
    char text[64];snprintf(text,sizeof(text),"{\"result\":5,\"id\":%lu}",(unsigned long)id);return httpd_resp_sendstr(req,text);
}
static esp_err_t web_cancel(httpd_req_t *req){
    uint32_t generation;if(!web_auth(req,&generation)){response(req,6);return ESP_FAIL;}
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
    int fd=httpd_req_to_sockfd(req);
    httpd_req_async_handler_complete(req);
    if(result)httpd_sess_trigger_close(server,fd);
    vTaskDelete(NULL);
}
static esp_err_t web_upload(httpd_req_t *req){
    uint32_t generation;if(!web_auth(req,&generation)){response(req,6);return ESP_FAIL;}
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
    if(xSemaphoreTake(lock,0)!=pdTRUE){wallpaper_cancel(2,job->session);free(job);response(req,1);return ESP_FAIL;}
    if(upload.owner!=2||upload.session!=job->session){xSemaphoreGive(lock);free(job);response(req,6);return ESP_FAIL;}
    upload.generation=generation;xSemaphoreGive(lock);
    if(httpd_req_async_handler_begin(req,&job->req)!=ESP_OK){wallpaper_cancel(2,job->session);free(job);response(req,3);return ESP_FAIL;}
    if(xTaskCreate(web_receive,"wall_http",6144,job,4,NULL)!=pdPASS){
        wallpaper_cancel(2,job->session);response(job->req,3);httpd_req_async_handler_complete(job->req);free(job);return ESP_FAIL;
    }
    return ESP_OK;
}
static bool restoring_wallpaper;
void wallpaper_service_init(void){
    snprintf(ap_name,sizeof(ap_name),"CABadge-%04lx",(unsigned long)(esp_random()&0xffff));
    snprintf(ap_password,sizeof(ap_password),"%08lx%04lx",(unsigned long)esp_random(),(unsigned long)(esp_random()&0xffff));
    lock=xSemaphoreCreateMutex();completed=xQueueCreate(1,sizeof(finish_result_t));partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"wallpaper");
    if(lock&&partition&&partition->size>=2*WALL_SLOT){
        uint8_t *pixels=heap_caps_malloc(WALL_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(pixels){storage_ok=wall_load(&record,pixels);if(storage_ok&&record.size){pending=pixels;changed=true;restoring_wallpaper=true;}else free(pixels);}
    }
    if(!changed)badge_ui_restore_photo(&badge_wallpaper);
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_open_sockets=3;cfg.lru_purge_enable=false;cfg.recv_wait_timeout=5;cfg.send_wait_timeout=5;
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
void wallpaper_service_poll(void){
    if(lock&&xSemaphoreTake(lock,0)==pdTRUE){
        if(upload.owner&&esp_timer_get_time()-upload.touched>30000000){clear_upload();phase=7;last_error=4;}
        if(changed){
            lv_image_cache_drop(&picture);uint8_t *old=display_pixels;display_pixels=pending;pending=NULL;changed=false;
            picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=WALL_BYTES,.data=display_pixels};
            if(restoring_wallpaper){badge_ui_restore_photo(display_pixels?&picture:&badge_wallpaper);restoring_wallpaper=false;}
            else badge_ui_set_photo(display_pixels?&picture:&badge_wallpaper);
            badge_ui_sleep(false);free(old);
            phase=4;last_error=0;
            snprintf(notice,sizeof(notice),"%s",display_pixels?"壁纸已保存并应用":"已恢复默认壁纸");
        }
        xSemaphoreGive(lock);
    }
    static int64_t last;int64_t now=esp_timer_get_time();
    if(now-last>1000000){
        hotspot=wifi_wallpaper_hotspot_active();
        last=now;char ip[24],url[128],key[33];wifi_wallpaper_address(ip,sizeof(ip));management_key(key);
        snprintf(url,sizeof(url),"http://%s/#key=%s",hotspot?"192.168.4.1":ip,key);
        badge_wallpaper_update(*key&&(*ip||hotspot)&&http_ok?url:"",hotspot,ap_name,ap_password,
            !storage_ok?"壁纸存储不可用，请检查固件":!http_ok?"上传服务启动失败":notice);
        badge_management_update(*key!=0,key,management_remaining());
    }
}
