#include "management.h"
#include "wallpaper_service.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdio.h>

static badge_state_t *state;
static SemaphoreHandle_t guard;
static uint32_t link_generation=1;
static uint32_t serial,last_id;
static int last_result,last_op;
static struct {uint8_t data[MG_MAX_COMMAND];size_t n;uint32_t generation,id;uint16_t handle;} pending;
enum { SNAPSHOT_BYTES=8192 };
static char *snapshot;
static uint8_t binary[MG_STATUS_BYTES];
static uint64_t now_ms(void){return esp_timer_get_time()/1000;}
static bool take(void){return guard&&xSemaphoreTake(guard,pdMS_TO_TICKS(20))==pdTRUE;}
static void give(void){xSemaphoreGive(guard);}
/* No access code. Epochs only discard queued work after a link is closed. */
void management_close(void){
    if(!take())return;
    link_generation++;
    if(pending.n){last_result=MG_FAILED;last_id=pending.id;memset(&pending,0,sizeof(pending));}give();
    wallpaper_cancel(2,0);
}
bool management_begin_request(uint32_t *generation){
    if(!take())return false;
    *generation=link_generation;give();return true;
}
bool management_generation_valid(uint32_t generation){
    if(!take())return false;
    bool ok=generation==link_generation;give();return ok;
}
bool management_ble_allowed(uint16_t handle,uint32_t *generation){return handle!=UINT16_MAX&&management_begin_request(generation);}
void management_ble_disconnect(uint16_t handle){
    if(!take())return;
    if(pending.n&&pending.handle==handle){last_id=pending.id;last_result=MG_FAILED;memset(&pending,0,sizeof(pending));}
    give();
}
int management_submit(const uint8_t *data,size_t n,uint32_t generation,uint16_t handle,uint32_t *id){
    if(!mg_valid_command(data,n))return MG_INVALID;
    if(wallpaper_busy())return MG_BUSY;
    if(!take())return MG_BUSY;
    int rc=MG_ACCEPTED;
    if(generation!=link_generation)rc=MG_FAILED;
    else if(pending.n)rc=MG_BUSY;
    else {memcpy(pending.data,data,n);pending.n=n;pending.generation=generation;pending.handle=handle;
        pending.id=++serial;*id=serial;last_id=serial;last_result=MG_ACCEPTED;last_op=data[1];}
    give();return rc;
}
bool management_json(char *out,size_t n){
    if(!take())return false;
    bool ok=*snapshot&&strlen(snapshot)<n;
    if(ok)memcpy(out,snapshot,strlen(snapshot)+1);
    give();return ok;
}
bool management_ble_status(uint16_t handle,uint8_t out[MG_STATUS_BYTES]){
    if(!take())return false;
    bool ok=handle!=UINT16_MAX;
    if(ok)memcpy(out,binary,MG_STATUS_BYTES);
    give();return ok;
}
static void boolean(cJSON *o,const char *name,bool value){cJSON_AddBoolToObject(o,name,value);}
static void number(cJSON *o,const char *name,int value){cJSON_AddNumberToObject(o,name,value);}
static void string(cJSON *o,const char *name,const char *value){
    /* Advertised names may contain malformed UTF-8; never emit invalid JSON text. */
    cJSON_AddStringToObject(o,name,mg_text((const uint8_t*)value,strlen(value),false)?value:"[invalid name]");
}
static void make_snapshot(void){
    badge_ble_info_t ble={0};ble_service_snapshot(&ble);
    bool scanning=false,connecting=false;int wifi_error=0,count=0;badge_wifi_ap_t aps[12];
    wifi_service_snapshot(&scanning,&connecting,&wifi_error,aps,&count);
    char ip[24];wifi_wallpaper_address(ip,sizeof(ip));
    int phase,wall_error;uint32_t received;wallpaper_status(&phase,&wall_error,&received);
    cJSON *root=cJSON_CreateObject();if(!root)return;
    number(root,"protocol",MG_VERSION);string(root,"product","CABadge");string(root,"firmware",UI_VERSION);
    if(state->battery_mv>=0)number(root,"battery_mv",state->battery_mv);else cJSON_AddNullToObject(root,"battery_mv");
    number(root,"brightness",state->brightness);boolean(root,"asleep",badge_ui_is_asleep());boolean(root,"reduced_motion",state->reduced_motion);
    string(root,"brightness_scope","backlight");
    cJSON *wifi=cJSON_AddObjectToObject(root,"wifi");
    boolean(wifi,"enabled",state->wifi_enabled);boolean(wifi,"scanning",scanning);boolean(wifi,"connecting",connecting);
    boolean(wifi,"connected",state->wifi_connected==1);string(wifi,"ssid",state->wifi_connected==1?state->wifi_ssid:"");
    number(wifi,"rssi",state->wifi_rssi);string(wifi,"ip",ip);number(wifi,"error",wifi_error);
    boolean(wifi,"hotspot",wifi_wallpaper_hotspot_active());cJSON *networks=cJSON_AddArrayToObject(wifi,"networks");
    for(int i=0;i<count;i++){cJSON *ap=cJSON_CreateObject();string(ap,"ssid",aps[i].ssid);number(ap,"rssi",aps[i].rssi);number(ap,"security",aps[i].security);boolean(ap,"saved",aps[i].saved);cJSON_AddItemToArray(networks,ap);}
    cJSON *b=cJSON_AddObjectToObject(root,"ble");
    boolean(b,"enabled",ble.enabled);boolean(b,"advertising",ble.advertising);boolean(b,"scanning",ble.scanning);boolean(b,"connecting",ble.connecting);
    string(b,"incoming",ble.inbound?ble.inbound_address:"");string(b,"outgoing",ble.outbound?ble.outbound_address:"");number(b,"error",ble.error);
    cJSON *devices=cJSON_AddArrayToObject(b,"devices");
    for(int i=0;i<ble.count;i++){
        badge_ble_device_t *d=&ble.devices[i];cJSON *item=cJSON_CreateObject();char addr[13];
        for(int j=0;j<6;j++)snprintf(addr+j*2,3,"%02x",d->address[j]);
        string(item,"name",d->name);string(item,"address",addr);number(item,"type",d->address_type);
        number(item,"rssi",d->rssi);boolean(item,"connectable",d->connectable);cJSON_AddItemToArray(devices,item);
    }
    cJSON *wall=cJSON_AddObjectToObject(root,"wallpaper");number(wall,"phase",phase);number(wall,"error",wall_error);number(wall,"received",received);
    number(wall,"selected",state->wallpaper_index);number(wall,"capacity",31);
    int ids[31],n=wallpaper_catalog(ids);
    if(n>=0){cJSON *items=cJSON_AddArrayToObject(wall,"items");for(int i=0;i<n;i++)cJSON_AddItemToArray(items,cJSON_CreateNumber(ids[i]));}
    uint32_t generation=0,size=0,crc=0;
    if(wallpaper_resource_info(&generation,&size,&crc)){cJSON_AddNumberToObject(wall,"generation",generation);cJSON_AddNumberToObject(wall,"size",size);cJSON_AddNumberToObject(wall,"crc",crc);}
    uint8_t bytes[MG_STATUS_BYTES]={MG_VERSION};
    bytes[1]=state->wifi_enabled|(scanning<<1)|(connecting<<2)|((state->wifi_connected==1)<<3)|(wifi_wallpaper_hotspot_active()<<4);
    bytes[2]=ble.enabled|(ble.advertising<<1)|(ble.scanning<<2)|(ble.connecting<<3)|(ble.inbound<<4)|(ble.outbound<<5);
    bytes[3]=state->brightness;bytes[4]=badge_ui_is_asleep()|(state->reduced_motion<<1)|((state->battery_mv>=0)<<2);
    bytes[5]=state->battery_mv;bytes[6]=state->battery_mv>>8;bytes[7]=(uint8_t)state->wifi_rssi;bytes[8]=phase;
    bytes[16]=ble.error;bytes[17]=wifi_error;bytes[18]=7;bytes[19]=1;
    bool changed=false;
    if(take()){
        number(root,"command_id",last_id);number(root,"command_op",last_op);number(root,"command_result",last_result);
        bytes[9]=last_result;for(int i=0;i<4;i++)bytes[10+i]=last_id>>(8*i);
        unsigned remaining=0;bytes[14]=remaining;bytes[15]=remaining>>8;
        if(!cJSON_PrintPreallocated(root,snapshot,SNAPSHOT_BYTES,false))snapshot[0]=0;
        changed=memcmp(bytes,binary,sizeof(binary))!=0;memcpy(binary,bytes,sizeof(binary));give();
    }
    cJSON_Delete(root);if(changed)ble_service_status_changed();
}
void management_init(badge_state_t *value){
    state=value;snapshot=heap_caps_malloc(SNAPSHOT_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!snapshot)return;
    snapshot[0]=0;guard=xSemaphoreCreateMutex();
}
void management_poll(void){
    uint8_t data[MG_MAX_COMMAND];size_t n=0;uint32_t id=0;
    if(take()){
        if(pending.n){
            id=pending.id;
            if(link_generation==pending.generation){
                n=pending.n;memcpy(data,pending.data,n);
            }else {last_id=id;last_result=MG_UNAUTHORIZED;}
            memset(&pending,0,sizeof(pending));
        }
        give();
    }
    if(n){
        int result=wallpaper_busy()?MG_BUSY:MG_OK;
        if(!result){
            if(data[1]<=MG_WIFI_DISCONNECT)result=wifi_service_control(data,n);
            else if(data[1]<=MG_BLE_DISCONNECT)result=ble_service_control(data,n);
            else if(data[1]==MG_BRIGHTNESS)badge_ui_settings(data[2],state->reduced_motion);
            else if(data[1]==MG_REDUCED)badge_ui_settings(state->brightness,data[2]);
            else if(data[1]==MG_SLEEP)badge_ui_sleep(data[2]);
            else if(data[1]==MG_WALL_SELECT||data[1]==MG_WALL_DELETE)result=wallpaper_select(data[2],data[1]==MG_WALL_DELETE);
        }
        if(take()){last_id=id;last_op=data[1];last_result=result;give();}memset(data,0,sizeof(data));
    }
    static uint64_t last;uint64_t now=now_ms();
    if(now-last>=500){last=now;make_snapshot();}
}
