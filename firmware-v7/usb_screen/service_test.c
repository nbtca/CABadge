#include "service_test_shim.h"
#define pending command_pending
#include "device/src/management.c"
#undef pending
#include "device/src/wallpaper_service.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void wifi_wallpaper_address(char *out,size_t n){snprintf(out,n,"192.168.1.2");}
bool wifi_wallpaper_hotspot_active(void){return mock_hotspot;}
esp_err_t wifi_wallpaper_hotspot(bool on,const char *name,const char *password){(void)name;(void)password;mock_hotspot=on;return 0;}
int wifi_service_control(const uint8_t *data,size_t n){(void)data;(void)n;radio_commands++;return MG_OK;}
void wifi_service_snapshot(bool *scan,bool *connect,int *error,badge_wifi_ap_t *aps,int *count){(void)aps;*scan=*connect=false;*error=*count=0;}
int ble_service_control(const uint8_t *data,size_t n){(void)data;(void)n;radio_commands++;return MG_OK;}
void ble_service_snapshot(badge_ble_info_t *out){memset(out,0,sizeof(*out));}
void ble_service_status_changed(void){}
static uint8_t pixels[WALL_BYTES],reloaded[WALL_BYTES];static char access_key[33],crc_text[9];
static httpd_req_t request(const char *uri,size_t size){return (httpd_req_t){.uri=uri,.content_len=size,.body=pixels,.key=access_key,.crc=crc_text,.type="application/octet-stream",.host="192.168.1.2",.fail_after=-1};}
int main(void){
    memset(mock_flash,255,sizeof(mock_flash));badge_state_t board={.brightness=70,.battery_mv=4032,.wifi_enabled=true};
    management_init(&board);wallpaper_service_init();CHECK(registered_count==6);CHECK(!management_active());
    for(int i=0;i<registered_count;i++)CHECK(strcmp(registered[i].uri,"/firmware"));
    httpd_req_t r=request("/status",0);web_status(&r);CHECK(!strcmp(r.status,"403 Forbidden"));
    management_toggle();management_key(access_key);CHECK(strlen(access_key)==32);uint32_t generation;
    CHECK(management_http_auth(access_key,&generation));management_poll();r=request("/status",0);CHECK(web_status(&r)==0);CHECK(strstr(r.response,"CABadge")&&r.security_headers>=4);
    r=request("/status",0);r.origin="http://evil.invalid";web_status(&r);CHECK(!strcmp(r.status,"403 Forbidden"));
    uint8_t session_write[17]={1};CHECK(mg_parse_key(access_key,session_write+1));uint8_t status_bytes[20];
    CHECK(!management_ble_status(7,status_bytes));CHECK(management_ble_authorize(7,session_write,17)==0);CHECK(management_ble_status(7,status_bytes));
    CHECK(status_bytes[18]==7&&status_bytes[19]==1);
    uint8_t command[]={1,MG_SLEEP,1};uint32_t id;
    CHECK(management_submit(command,3,generation,7,&id)==MG_ACCEPTED);
    CHECK(management_submit(command,3,generation,7,&id)==MG_BUSY);
    management_ble_disconnect(7);management_poll();CHECK(!asleep);CHECK(!management_ble_status(7,status_bytes));
    CHECK(management_submit(command,3,generation,UINT16_MAX,&id)==MG_ACCEPTED);management_poll();CHECK(asleep);
    command[2]=0;CHECK(management_submit(command,3,generation,UINT16_MAX,&id)==MG_ACCEPTED);
    management_close();management_poll();CHECK(asleep);management_toggle();management_key(access_key);CHECK(management_http_auth(access_key,&generation));
    command[1]=MG_BRIGHTNESS;command[2]=101;CHECK(management_submit(command,3,generation,UINT16_MAX,&id)==MG_INVALID);
    memset(pixels,0x73,sizeof(pixels));snprintf(crc_text,sizeof(crc_text),"%08x",wall_crc(pixels,sizeof(pixels)));
    r=request("/upload",12);web_upload(&r);CHECK(!strcmp(r.status,"400 Bad Request"));
    r=request("/upload",WALL_BYTES);r.type="image/png";web_upload(&r);CHECK(!strcmp(r.status,"400 Bad Request"));
    r=request("/upload",WALL_BYTES);r.crc="-1234567";web_upload(&r);CHECK(!strcmp(r.status,"400 Bad Request"));
    r=request("/control",3);uint8_t invalid_command[]={1,99,0};r.body=invalid_command;web_control(&r);CHECK(!strcmp(r.status,"400 Bad Request"));
    r=request("/control",3);uint8_t valid_command[]={1,MG_SLEEP,0};r.body=valid_command;web_control(&r);CHECK(!strcmp(r.status,"202 Accepted"));management_poll();CHECK(!asleep);
    uint32_t usb;CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==0);
    r=request("/upload",WALL_BYTES);web_upload(&r);CHECK(!strcmp(r.status,"409 Conflict"));wallpaper_cancel(1,usb);
    r=request("/upload",WALL_BYTES);CHECK(web_upload(&r)==0&&queued_task);CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==1);
    run_task();CHECK(!strcmp(r.status,"200 OK"));wallpaper_service_poll();CHECK(phase==4&&shown==&picture&&shown->data[0]==0x73);
    wall_record_t disk;CHECK(wall_load(&disk,reloaded)&&!memcmp(reloaded,pixels,WALL_BYTES));
    uint32_t original_generation=disk.generation;
    uint32_t resource_generation,resource_size,resource_crc;uint8_t readback[4096];
    CHECK(wallpaper_resource_info(&resource_generation,&resource_size,&resource_crc));
    CHECK(resource_generation==disk.generation&&resource_size==WALL_BYTES&&resource_crc==wall_crc(pixels,WALL_BYTES));
    CHECK(wallpaper_read_resource(resource_generation,0,readback,sizeof(readback))&&!memcmp(readback,pixels,sizeof(readback)));
    CHECK(!wallpaper_read_resource(resource_generation+1,0,readback,sizeof(readback)));
    CHECK(!wallpaper_read_resource(resource_generation,WALL_BYTES-1,readback,2));

    r=request("/upload",WALL_BYTES);r.crc="00000000";web_upload(&r);run_task();CHECK(!strcmp(r.status,"400 Bad Request"));
    CHECK(wall_load(&disk,reloaded)&&disk.generation==original_generation);
    r=request("/upload",WALL_BYTES);r.fail_after=4096;web_upload(&r);run_task();CHECK(!strcmp(r.status,"408 Request Timeout"));CHECK(!wallpaper_busy());
    r=request("/upload",WALL_BYTES);web_upload(&r);management_close();run_task();CHECK(!strcmp(r.status,"403 Forbidden"));CHECK(wall_load(&disk,reloaded)&&disk.generation==original_generation);
    management_toggle();management_key(access_key);CHECK(!management_generation_valid(generation));
    CHECK(management_ble_authorize(7,session_write,17)==MG_UNAUTHORIZED);
    r=request("/upload",WALL_BYTES);web_upload(&r);httpd_req_t cancel=request("/cancel",0);web_cancel(&cancel);CHECK(!strcmp(cancel.status,"200 OK"));run_task();CHECK(!wallpaper_busy());
    CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==0);CHECK(wallpaper_chunk(1,usb,5,pixels,10)==4);CHECK(!wallpaper_busy());
    CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==0);CHECK(wallpaper_chunk(1,usb,0,pixels,10)==0);CHECK(wallpaper_chunk(1,usb,0,pixels,10)==4);CHECK(!wallpaper_busy());
    CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==0);clock_us+=31000000;CHECK(wallpaper_chunk(1,usb,0,pixels,10)==4);CHECK(!wallpaper_busy());
    allocation_failure=true;CHECK(wallpaper_begin(1,WALL_BYTES,0,&usb)==3);allocation_failure=false;
    flash_failure=true;r=request("/upload",WALL_BYTES);web_upload(&r);run_task();CHECK(!strcmp(r.status,"500 Internal Server Error"));flash_failure=false;
    CHECK(wall_load(&disk,reloaded)&&disk.generation==original_generation);
    task_failure=true;r=request("/upload",WALL_BYTES);web_upload(&r);CHECK(!wallpaper_busy());task_failure=false;
    CHECK(wallpaper_begin(1,0,0,&usb)==0);CHECK(wallpaper_finish_async(usb)==0);run_task();uint32_t finished;int result;
    CHECK(!wallpaper_finish_result(&finished,&result));wallpaper_service_poll();CHECK(wallpaper_finish_result(&finished,&result)&&result==0&&finished==usb);
    CHECK(wall_load(&disk,reloaded)&&disk.size==0);
    clock_us+=300000000;management_poll();CHECK(!management_active());r=request("/status",0);web_status(&r);CHECK(!strcmp(r.status,"403 Forbidden"));
    puts("production services: HTTP auth/origin/type/CRC/busy/cancel/expiry; BLE authorization; command queue; upload timeout/failures; atomic reload PASS");
}
