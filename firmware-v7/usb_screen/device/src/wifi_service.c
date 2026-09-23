#include "ui/badge_ui.h"
#include "wallpaper_service.h"
#include "management.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static badge_state_t *state;
static QueueHandle_t events;
static nvs_handle_t storage;
static bool initialized,connecting,ignore_disconnect,scanning;
static unsigned retries;
static int last_error;
static bool manual_disconnect,hotspot_started_wifi;
static char pending_ssid[33],pending_password[64];
/* Eight successful personal/open networks, most recently used first. */
typedef struct {char ssid[33],password[64];} saved_network_t;
static saved_network_t history[8];
static int64_t reconnect_at;
static int remembered(const char *ssid){
    for(int i=0;i<8;i++)if(*history[i].ssid&&!strcmp(history[i].ssid,ssid))return i;
    return -1;
}
static bool remember(void){
    if(!storage)return false;
    if(!strcmp(history[0].ssid,pending_ssid)&&!strcmp(history[0].password,pending_password))return true;
    int at=remembered(pending_ssid);if(at<0)at=7;
    memmove(history+1,history,at*sizeof(*history));
    snprintf(history[0].ssid,sizeof(history[0].ssid),"%s",pending_ssid);
    snprintf(history[0].password,sizeof(history[0].password),"%s",pending_password);
    return nvs_set_blob(storage,"history",history,sizeof(history))==ESP_OK&&nvs_commit(storage)==ESP_OK;
}
static badge_wifi_ap_t visible[12];
static int visible_count;
static esp_netif_t *station,*access_point;
bool wifi_wallpaper_hotspot_active(void){
    wifi_mode_t mode;
    return initialized&&state->wifi_enabled&&esp_wifi_get_mode(&mode)==ESP_OK&&mode==WIFI_MODE_APSTA;
}

void wifi_wallpaper_address(char *ip,size_t n){
    esp_netif_ip_info_t info;ip[0]=0;
    if(station&&esp_netif_is_netif_up(station)&&esp_netif_get_ip_info(station,&info)==ESP_OK&&info.ip.addr)
        snprintf(ip,n,IPSTR,IP2STR(&info.ip));
}
esp_err_t wifi_wallpaper_hotspot(bool on,const char *name,const char *password){
    (void)password;
    if(!initialized)return ESP_ERR_INVALID_STATE;
    if(on){
        if(!access_point)access_point=esp_netif_create_default_wifi_ap();
        if(!access_point)return ESP_ERR_NO_MEM;
        esp_err_t rc=esp_wifi_set_mode(WIFI_MODE_APSTA);if(rc!=ESP_OK)return rc;
        wifi_config_t config={0};snprintf((char*)config.ap.ssid,sizeof(config.ap.ssid),"%s",name);
        config.ap.ssid_len=strlen(name);config.ap.channel=6;config.ap.max_connection=2;config.ap.authmode=WIFI_AUTH_OPEN;
        rc=esp_wifi_set_config(WIFI_IF_AP,&config);if(rc!=ESP_OK){esp_wifi_set_mode(WIFI_MODE_STA);return rc;}
        if(!state->wifi_enabled){rc=esp_wifi_start();if(rc!=ESP_OK){esp_wifi_set_mode(WIFI_MODE_STA);return rc;}hotspot_started_wifi=true;state->wifi_enabled=true;badge_ui_refresh();}
        return ESP_OK;
    }
    esp_err_t rc=esp_wifi_set_mode(WIFI_MODE_STA);
    if(rc==ESP_OK&&hotspot_started_wifi){
        rc=esp_wifi_stop();hotspot_started_wifi=false;state->wifi_enabled=false;state->wifi_connected=0;state->wifi_rssi=-127;
        manual_disconnect=true;badge_ui_refresh();
    }
    return rc;
}
enum { SCAN_DONE=1,CONNECTED,DISCONNECTED };
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;(void)data;int value=0;
    if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE)value=SCAN_DONE;
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED)value=DISCONNECTED;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)value=CONNECTED;
    if(value)xQueueSend(events,&value,0);
}
static void scan(void){
    if(!initialized||!state->wifi_enabled){badge_ui_wifi_message("Wi-Fi 已关闭或初始化失败");return;}
    if(connecting){badge_ui_wifi_message("正在连接，请稍候");return;}
    if(scanning)return;
    wifi_scan_config_t config={.show_hidden=false};
    esp_err_t error=esp_wifi_scan_start(&config,false);
    last_error=error;scanning=error==ESP_OK;
    if(!scanning)badge_ui_wifi_message("扫描失败，点刷新重试");
}
static void connect_network(const char *ssid,const char *password){
    int saved=remembered(ssid);
    if(!*password&&saved>=0)password=history[saved].password;
    size_t length=strlen(ssid),pass_length=strlen(password);
    if(!initialized||!state->wifi_enabled||!length||length>32||pass_length>63||(pass_length&&pass_length<8)){
        badge_ui_wifi_message("网络名称或密码无效");return;
    }
    if(scanning){esp_wifi_scan_stop();scanning=false;}
    manual_disconnect=false;reconnect_at=esp_timer_get_time()+15000000;
    wifi_ap_record_t current;
    ignore_disconnect=esp_wifi_sta_get_ap_info(&current)==ESP_OK;
    esp_wifi_disconnect();
    wifi_config_t config={0};memcpy(config.sta.ssid,ssid,length);memcpy(config.sta.password,password,pass_length);
    config.sta.threshold.authmode=pass_length?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable=true;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    snprintf(pending_ssid,sizeof(pending_ssid),"%s",ssid);
    snprintf(pending_password,sizeof(pending_password),"%s",password);
    esp_err_t error=esp_wifi_set_config(WIFI_IF_STA,&config);memset(&config,0,sizeof(config));
    if(error==ESP_OK)error=esp_wifi_connect();
    last_error=error;connecting=error==ESP_OK;retries=0;state->wifi_connected=0;state->wifi_rssi=-127;
    badge_ui_refresh();badge_ui_wifi_message(connecting?"正在连接…":"无法开始连接，请重试");
}
static void enable(bool on){
    if(!initialized){state->wifi_enabled=false;badge_ui_refresh();return;}
    state->wifi_enabled=on;connecting=false;scanning=false;ignore_disconnect=false;hotspot_started_wifi=false;manual_disconnect=!on;
    if(on){
        reconnect_at=0;
        last_error=esp_wifi_start();if(last_error!=ESP_OK){state->wifi_enabled=false;badge_ui_wifi_message("Wi-Fi 启动失败");}
    }else{
        management_close();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_stop();state->wifi_connected=0;state->wifi_rssi=-127;
        memset(pending_password,0,sizeof(pending_password));
    }
    if(storage){nvs_set_u8(storage,"enabled",state->wifi_enabled);nvs_commit(storage);}
    badge_ui_refresh();
}
void wifi_service_init(badge_state_t *value){
    state=value;state->wifi_connected=0;
    const badge_wifi_actions_t actions={.scan=scan,.connect=connect_network,.enable=enable};badge_ui_wifi_bind(&actions);
    events=xQueueCreate(16,sizeof(int));
    if(!events||esp_netif_init()!=ESP_OK||esp_event_loop_create_default()!=ESP_OK)return;
    station=esp_netif_create_default_wifi_sta();if(!station)return;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&config)!=ESP_OK)return;
    if(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL)!=ESP_OK||
       esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL)!=ESP_OK)return;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK)return;
    initialized=true;
    if(nvs_open("badge_wifi",NVS_READWRITE,&storage)==ESP_OK){
        uint8_t on=1;nvs_get_u8(storage,"enabled",&on);state->wifi_enabled=on!=0;
        size_t bytes=sizeof(history);
        if(nvs_get_blob(storage,"history",history,&bytes)!=ESP_OK||bytes!=sizeof(history)){
            memset(history,0,sizeof(history));
            size_t a=sizeof(history[0].ssid),b=sizeof(history[0].password);
            if(nvs_get_str(storage,"ssid",history[0].ssid,&a)!=ESP_OK||nvs_get_str(storage,"password",history[0].password,&b)!=ESP_OK)memset(history,0,sizeof(history));
        }
        for(int i=0;i<8;i++){history[i].ssid[32]=0;history[i].password[63]=0;}
    }
    if(!state->wifi_enabled)return;
    last_error=esp_wifi_start();if(last_error!=ESP_OK){state->wifi_enabled=false;return;}
    if(*history[0].ssid)connect_network(history[0].ssid,history[0].password);
}
void wifi_service_poll(void){
    if(!initialized)return;
    int value;
    while(xQueueReceive(events,&value,0)==pdTRUE){
        if(value==SCAN_DONE){
            scanning=false;
            if(!state->wifi_enabled||connecting){esp_wifi_clear_ap_list();continue;}
            wifi_ap_record_t found[24];uint16_t count=24;visible_count=0;
            if(esp_wifi_scan_get_ap_records(&count,found)!=ESP_OK){badge_ui_wifi_message("扫描失败，请重试");continue;}
            int best=-1,best_rssi=-128;
            for(int i=0;i<count;i++){
                char name[33];memcpy(name,found[i].ssid,32);name[32]=0;int saved=remembered(name);
                if(saved>=0&&found[i].rssi>best_rssi){best=saved;best_rssi=found[i].rssi;}
            }
            for(int i=0;i<count&&visible_count<12;i++){
                char ssid[33];memcpy(ssid,found[i].ssid,32);ssid[32]=0;if(!*ssid)continue;
                bool duplicate=false;for(int j=0;j<visible_count;j++)if(!strcmp(visible[j].ssid,ssid))duplicate=true;
                if(duplicate)continue;
                badge_wifi_ap_t *ap=&visible[visible_count++];snprintf(ap->ssid,sizeof(ap->ssid),"%s",ssid);ap->rssi=found[i].rssi;
                wifi_auth_mode_t mode=found[i].authmode;
                bool enterprise=mode==WIFI_AUTH_WPA2_ENTERPRISE||mode==WIFI_AUTH_WPA3_ENT_192||
                    mode==WIFI_AUTH_WPA3_ENTERPRISE||mode==WIFI_AUTH_WPA2_WPA3_ENTERPRISE||mode==WIFI_AUTH_WPA_ENTERPRISE;
                ap->security=mode==WIFI_AUTH_OPEN?0:enterprise?2:1;ap->saved=remembered(ssid)>=0;
            }
            badge_ui_wifi_results(visible,visible_count);
            if(best>=0&&!manual_disconnect&&state->wifi_connected!=1)connect_network(history[best].ssid,history[best].password);
        }else if(value==CONNECTED){
            wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)continue;
            last_error=0;state->wifi_connected=1;state->wifi_rssi=ap.rssi;
            memcpy(state->wifi_ssid,ap.ssid,32);state->wifi_ssid[32]=0;connecting=false;retries=0;
            bool saved=false;
            if(storage&&!strcmp(state->wifi_ssid,pending_ssid)){
                saved=remember();
                for(int i=0;i<visible_count;i++)visible[i].saved=remembered(visible[i].ssid)>=0;
            }
            badge_ui_refresh();badge_ui_wifi_results(visible,visible_count);
            badge_ui_wifi_message(saved?"已连接，已记住此网络":"已连接；网络信息未保存");
        }else if(value==DISCONNECTED){
            if(ignore_disconnect){ignore_disconnect=false;continue;}
            state->wifi_connected=0;state->wifi_rssi=-127;badge_ui_refresh();
            if(manual_disconnect){connecting=false;last_error=0;badge_ui_wifi_message("已断开");continue;}
            if(state->wifi_enabled&&*pending_ssid&&retries++<2){
                connecting=true;esp_wifi_connect();badge_ui_wifi_message("连接中，请稍候…");
            }else{
                reconnect_at=esp_timer_get_time()+15000000;connecting=false;last_error=state->wifi_enabled?ESP_FAIL:0;badge_ui_wifi_message(state->wifi_enabled?"连接失败，请检查密码或信号":"Wi-Fi 已关闭");
            }
        }
    }
    static int64_t last_rssi;
    int64_t now=esp_timer_get_time();
    if(state->wifi_enabled&&!manual_disconnect&&state->wifi_connected!=1&&!connecting&&!scanning&&*history[0].ssid&&now>=reconnect_at){
        reconnect_at=now+15000000;scan();
    }
    if(state->wifi_connected==1&&now-last_rssi>=2000000){
        wifi_ap_record_t ap;last_rssi=now;
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&state->wifi_rssi!=ap.rssi){state->wifi_rssi=ap.rssi;badge_ui_refresh();}
    }
}

void wifi_service_snapshot(bool *scan_active,bool *connect_active,int *error,badge_wifi_ap_t *aps,int *count){
    *scan_active=scanning;*connect_active=connecting;*error=initialized?last_error:ESP_ERR_INVALID_STATE;
    *count=state->wifi_enabled?visible_count:0;memcpy(aps,visible,*count*sizeof(*aps));
}
int wifi_service_control(const uint8_t *data,size_t n){
    if(!initialized)return MG_FAILED;
    if(data[1]==MG_WIFI_ENABLE){enable(data[2]);return state->wifi_enabled==!!data[2]?MG_OK:MG_FAILED;}
    if(!state->wifi_enabled)return MG_FAILED;
    if(data[1]==MG_WIFI_SCAN){if(scanning||connecting)return MG_BUSY;scan();return scanning?MG_OK:MG_FAILED;}
    if(data[1]==MG_WIFI_CONNECT){
        if(connecting)return MG_BUSY;
        char ssid[33]={0},password[64]={0};memcpy(ssid,data+4,data[2]);memcpy(password,data+4+data[2],data[3]);
        connect_network(ssid,password);memset(password,0,sizeof(password));return connecting?MG_OK:MG_FAILED;
    }
    if(data[1]==MG_WIFI_DISCONNECT){
        manual_disconnect=true;
        pending_ssid[0]=0;memset(pending_password,0,sizeof(pending_password));retries=3;connecting=false;ignore_disconnect=false;
        last_error=esp_wifi_disconnect();state->wifi_connected=0;state->wifi_rssi=-127;badge_ui_refresh();return last_error?MG_FAILED:MG_OK;
    }
    return MG_INVALID;
}
