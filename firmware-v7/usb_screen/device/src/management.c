#include "management.h"
#include "wallpaper_service.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdio.h>

static badge_state_t *state;
static SemaphoreHandle_t guard;
static mg_session_t session;
static uint16_t authorized_handle=UINT16_MAX;
static unsigned bad_attempts;
static bool stop_requested;
static uint64_t retry_at;
static uint32_t serial,last_id;
static int last_result,last_op;
static struct {uint8_t data[MG_MAX_COMMAND];size_t n;uint32_t generation,id;uint16_t handle;} pending;
static char snapshot[8192];
static uint8_t binary[MG_STATUS_BYTES];
static uint64_t now_ms(void){return esp_timer_get_time()/1000;}
static bool take(void){return guard&&xSemaphoreTake(guard,pdMS_TO_TICKS(20))==pdTRUE;}
static void give(void){xSemaphoreGive(guard);}
bool management_active(void){if(!take())return false;bool on=mg_active(&session,now_ms());give();return on;}
unsigned management_remaining(void){if(!take())return 0;uint64_t now=now_ms();unsigned n=mg_active(&session,now)?(session.deadline-now+999)/1000:0;give();return n;}
void management_key(char out[33]){
    out[0]=0;if(!take())return;
    if(mg_active(&session,now_ms())){for(int i=0;i<16;i++)snprintf(out+i*2,3,"%02x",session.token[i]);}
    give();
}
void management_close(void){
    if(!take())return;
    mg_close(&session);authorized_handle=UINT16_MAX;stop_requested=true;
    if(pending.n){last_result=MG_UNAUTHORIZED;last_id=pending.id;memset(&pending,0,sizeof(pending));}give();
    wallpaper_cancel(2,0);
}
void management_toggle(void){
    if(management_active()){management_close();wallpaper_stop_hotspot();return;}
    uint8_t key[16];esp_fill_random(key,sizeof(key));
    if(take()){mg_open(&session,key,now_ms());authorized_handle=UINT16_MAX;bad_attempts=0;retry_at=0;stop_requested=false;give();}
    memset(key,0,sizeof(key));
}
static bool authenticate(const uint8_t token[16]){
    uint64_t now=now_ms();if(now<retry_at)return false;
    if(mg_authorized(&session,token,now)){bad_attempts=0;return true;}
    if(++bad_attempts>=5){retry_at=now+5000;bad_attempts=0;}return false;
}
bool management_http_auth(const char *key,uint32_t *generation){
    uint8_t token[16];if(!mg_parse_key(key,token)||!take())return false;
    bool ok=authenticate(token);if(ok)*generation=session.generation;give();memset(token,0,sizeof(token));return ok;
}
bool management_generation_valid(uint32_t generation){
    if(!take())return false;
    bool ok=mg_active(&session,now_ms())&&generation==session.generation;give();return ok;
}
int management_ble_authorize(uint16_t handle,const uint8_t *data,size_t n){
    if(n!=17||data[0]!=MG_VERSION)return MG_INVALID;
    if(!take())return MG_BUSY;
    bool ok=authenticate(data+1);if(ok)authorized_handle=handle;give();return ok?MG_OK:MG_UNAUTHORIZED;
}
bool management_ble_allowed(uint16_t handle,uint32_t *generation){
    if(!take())return false;
    bool ok=handle==authorized_handle&&handle!=UINT16_MAX&&mg_active(&session,now_ms());
    if(ok)*generation=session.generation;
    give();return ok;
}
void management_ble_disconnect(uint16_t handle){
    if(!take())return;
    if(handle==authorized_handle){authorized_handle=UINT16_MAX;
        if(pending.n&&pending.handle==handle){last_id=pending.id;last_result=MG_UNAUTHORIZED;memset(&pending,0,sizeof(pending));}}
    give();
}
int management_submit(const uint8_t *data,size_t n,uint32_t generation,uint16_t handle,uint32_t *id){
    if(!mg_valid_command(data,n))return MG_INVALID;
    if(wallpaper_busy())return MG_BUSY;
    if(!take())return MG_BUSY;
    int rc=MG_ACCEPTED;
    if(!mg_active(&session,now_ms())||generation!=session.generation||(handle!=UINT16_MAX&&handle!=authorized_handle))rc=MG_UNAUTHORIZED;
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
    bool ok=handle==authorized_handle&&handle!=UINT16_MAX&&mg_active(&session,now_ms());
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
    number(root,"remaining",management_remaining());
    if(state->battery_mv>=0)number(root,"battery_mv",state->battery_mv);else cJSON_AddNullToObject(root,"battery_mv");
    number(root,"brightness",state->brightness);boolean(root,"asleep",badge_ui_is_asleep());boolean(root,"reduced_motion",state->reduced_motion);
    string(root,"brightness_scope","preview");
    cJSON *wifi=cJSON_AddObjectToObject(root,"wifi");
    boolean(wifi,"enabled",state->wifi_enabled);boolean(wifi,"scanning",scanning);boolean(wifi,"connecting",connecting);
    boolean(wifi,"connected",state->wifi_connected==1);string(wifi,"ssid",state->wifi_connected==1?state->wifi_ssid:"");
    number(wifi,"rssi",state->wifi_rssi);string(wifi,"ip",ip);number(wifi,"error",wifi_error);
    boolean(wifi,"hotspot",wifi_wallpaper_hotspot_active());cJSON *networks=cJSON_AddArrayToObject(wifi,"networks");
    for(int i=0;i<count;i++){cJSON *ap=cJSON_CreateObject();string(ap,"ssid",aps[i].ssid);number(ap,"rssi",aps[i].rssi);number(ap,"security",aps[i].security);cJSON_AddItemToArray(networks,ap);}
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
    number(wall,"selected",state->wallpaper_index);
    uint32_t generation=0,size=0,crc=0;
    if(wallpaper_resource_info(&generation,&size,&crc)){cJSON_AddNumberToObject(wall,"generation",generation);cJSON_AddNumberToObject(wall,"size",size);cJSON_AddNumberToObject(wall,"crc",crc);}
    uint8_t bytes[MG_STATUS_BYTES]={MG_VERSION};
    bytes[1]=state->wifi_enabled|(scanning<<1)|(connecting<<2)|((state->wifi_connected==1)<<3)|(wifi_wallpaper_hotspot_active()<<4);
    bytes[2]=ble.enabled|(ble.advertising<<1)|(ble.scanning<<2)|(ble.connecting<<3)|(ble.inbound<<4)|(ble.outbound<<5);
    bytes[3]=state->brightness;bytes[4]=badge_ui_is_asleep()|(state->reduced_motion<<1)|((state->battery_mv>=0)<<2);
    bytes[5]=state->battery_mv;bytes[6]=state->battery_mv>>8;bytes[7]=(uint8_t)state->wifi_rssi;bytes[8]=phase;
    bytes[16]=ble.error;bytes[17]=wifi_error;bytes[18]=7;bytes[19]=1;
    char *text=NULL;bool changed=false;
    if(take()){
        number(root,"command_id",last_id);number(root,"command_op",last_op);number(root,"command_result",last_result);
        bytes[9]=last_result;for(int i=0;i<4;i++)bytes[10+i]=last_id>>(8*i);
        uint64_t now=now_ms();unsigned remaining=mg_active(&session,now)?(session.deadline-now)/1000:0;bytes[14]=remaining;bytes[15]=remaining>>8;
        text=cJSON_PrintUnformatted(root);
        if(text&&strlen(text)<sizeof(snapshot))memcpy(snapshot,text,strlen(text)+1);
        changed=memcmp(bytes,binary,sizeof(binary))!=0;memcpy(binary,bytes,sizeof(binary));give();
    }
    cJSON_free(text);cJSON_Delete(root);if(changed)ble_service_status_changed();
}
void management_init(badge_state_t *value){state=value;guard=xSemaphoreCreateMutex();badge_management_bind(management_toggle);}
void management_poll(void){
    bool expired=false,stop=false;uint8_t data[MG_MAX_COMMAND];size_t n=0;uint32_t id=0;
    if(take()){
        expired=session.open&&!mg_active(&session,now_ms());
        stop=stop_requested;stop_requested=false;
        if(pending.n){
            id=pending.id;
            if(mg_active(&session,now_ms())&&session.generation==pending.generation&&(pending.handle==UINT16_MAX||pending.handle==authorized_handle)){
                n=pending.n;memcpy(data,pending.data,n);
            }else {last_id=id;last_result=MG_UNAUTHORIZED;}
            memset(&pending,0,sizeof(pending));
        }
        give();
    }
    if(expired){management_close();wallpaper_stop_hotspot();}
    if(stop)wallpaper_stop_hotspot();
    if(n){
        int result=wallpaper_busy()?MG_BUSY:MG_OK;
        if(!result){
            if(data[1]<=MG_WIFI_DISCONNECT)result=wifi_service_control(data,n);
            else if(data[1]<=MG_BLE_DISCONNECT)result=ble_service_control(data,n);
            else if(data[1]==MG_BRIGHTNESS)badge_ui_settings(data[2],state->reduced_motion);
            else if(data[1]==MG_REDUCED)badge_ui_settings(state->brightness,data[2]);
            else if(data[1]==MG_SLEEP)badge_ui_sleep(data[2]);
        }
        if(take()){last_id=id;last_op=data[1];last_result=result;give();}memset(data,0,sizeof(data));
    }
    static uint64_t last;uint64_t now=now_ms();
    if(now-last>=500){last=now;make_snapshot();}
}
