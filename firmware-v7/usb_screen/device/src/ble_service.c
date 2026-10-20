/* GAP peripheral from the tested diagnostic, plus one outbound BLE link.
 * NimBLE owns radio state. Only the UI task calls LVGL. */
#include "ui/badge_ui.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "management.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static badge_state_t *state;
static QueueHandle_t updates,commands;
static badge_ble_info_t info={.enabled=true};
static uint8_t address_type;
static uint16_t incoming=BLE_HS_CONN_HANDLE_NONE,outgoing=BLE_HS_CONN_HANDLE_NONE;
static bool initialized,synchronized;
static struct ble_npl_event control;
static struct ble_npl_event notify_event;
static badge_ble_info_t observed;
static uint16_t status_value;
static bool subscribed;
/* Host-owned GATT client. No worker task, blocking scan or extra BLE stack. */
static struct ble_npl_callout hr_retry;
static badge_ble_device_t hr_target;
static bool hr_wanted;
static uint32_t hr_generation,hr_retry_ms=2000;
static uint16_t hr_start,hr_end,hr_value,hr_value_end,hr_cccd;
static portMUX_TYPE hr_guard=portMUX_INITIALIZER_UNLOCKED;
static badge_heart_rate_t hr_observed={.stale=true};
static const ble_uuid16_t hr_service_uuid=BLE_UUID16_INIT(0x180d);
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(1,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static const ble_uuid128_t status_uuid=BLE_UUID128_INIT(2,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static const ble_uuid128_t control_uuid=BLE_UUID128_INIT(3,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static int gatt_access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg){
    (void)attr;int kind=(int)(intptr_t)arg;
    if(conn!=incoming)return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    if(kind==1&&ctx->op==BLE_GATT_ACCESS_OP_READ_CHR){
        uint8_t status[MG_STATUS_BYTES];if(!management_ble_status(conn,status))return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        return os_mbuf_append(ctx->om,status,sizeof(status))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t data[MG_MAX_COMMAND];uint16_t length=OS_MBUF_PKTLEN(ctx->om),copied=0;
    if(length>sizeof(data)||ble_hs_mbuf_to_flat(ctx->om,data,sizeof(data),&copied)||copied!=length)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    int result;uint32_t generation,id;
    if(!management_ble_allowed(conn,&generation))result=MG_UNAUTHORIZED;
    else result=management_submit(data,length,generation,conn,&id);
    memset(data,0,sizeof(data));
    return result==MG_OK||result==MG_ACCEPTED?0:result==MG_UNAUTHORIZED?BLE_ATT_ERR_INSUFFICIENT_AUTHOR:result==MG_INVALID?BLE_ATT_ERR_VALUE_NOT_ALLOWED:0x80+result;
}
static const struct ble_gatt_chr_def characteristics[]={
    {.uuid=&status_uuid.u,.access_cb=gatt_access,.arg=(void*)1,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&status_value},
    {.uuid=&control_uuid.u,.access_cb=gatt_access,.arg=(void*)2,.flags=BLE_GATT_CHR_F_WRITE},
    {0}
};
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=characteristics},{0}
};
static void notify_status(struct ble_npl_event *event){
    (void)event;uint8_t data[MG_STATUS_BYTES];
    if(subscribed&&management_ble_status(incoming,data)){
        struct os_mbuf *om=ble_hs_mbuf_from_flat(data,sizeof(data));if(om)ble_gatts_notify_custom(incoming,status_value,om);
    }
}
void ble_service_status_changed(void){if(initialized)ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&notify_event);}
enum { ENABLE=1,SCAN,CONNECT,DISCONNECT };
typedef struct {int kind;bool flag;badge_ble_device_t device;} command_t;
static void advertise(void);
static int gap_event(struct ble_gap_event *event,void *arg);
static void publish(void){
    info.inbound=incoming!=BLE_HS_CONN_HANDLE_NONE;info.outbound=outgoing!=BLE_HS_CONN_HANDLE_NONE;
    info.heart_rate.stale=!badge_hr_fresh(&info.heart_rate,(uint32_t)(esp_timer_get_time()/1000));
    portENTER_CRITICAL(&hr_guard);hr_observed=info.heart_rate;portEXIT_CRITICAL(&hr_guard);
    xQueueOverwrite(updates,&info);
}
static void address(char *text,const uint8_t *a){snprintf(text,18,"%02X:%02X:%02X:%02X:%02X:%02X",a[5],a[4],a[3],a[2],a[1],a[0]);}
static bool hr_uuid_is(const ble_uuid_t *uuid,uint16_t value){
    if(uuid->type==BLE_UUID_TYPE_16)return BLE_UUID16(uuid)->value==value;
    if(uuid->type==BLE_UUID_TYPE_32)return BLE_UUID32(uuid)->value==value;
    const ble_uuid128_t expanded=BLE_UUID128_INIT(0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,value&255,value>>8,0,0);
    return uuid->type==BLE_UUID_TYPE_128&&!ble_uuid_cmp(uuid,&expanded.u);
}
static bool advertises_heart_rate(const struct ble_hs_adv_fields *fields){
    for(int i=0;i<fields->num_uuids16;i++)if(hr_uuid_is(&fields->uuids16[i].u,0x180d))return true;
    for(int i=0;i<fields->num_uuids32;i++)if(hr_uuid_is(&fields->uuids32[i].u,0x180d))return true;
    for(int i=0;i<fields->num_uuids128;i++)if(hr_uuid_is(&fields->uuids128[i].u,0x180d))return true;
    return fields->svc_data_uuid16_len>=2&&fields->svc_data_uuid16[0]==0x0d&&fields->svc_data_uuid16[1]==0x18;
}
static void remember_device(const struct ble_gap_disc_desc *report){
    if(!info.scanning)return;
    struct ble_hs_adv_fields fields;
    if(ble_hs_adv_parse_fields(&fields,report->data,report->length_data))return;
    bool heart_rate=advertises_heart_rate(&fields);
    int index=-1;
    for(int i=0;i<info.count;i++)if(info.devices[i].address_type==report->addr.type&&!memcmp(info.devices[i].address,report->addr.val,6)){index=i;break;}
    if(index<0){
        if(info.count<BADGE_BLE_DEVICES)index=info.count++;
        else {
            index=0;for(int i=1;i<info.count;i++)if(info.devices[i].heart_rate<info.devices[index].heart_rate||
                (info.devices[i].heart_rate==info.devices[index].heart_rate&&info.devices[i].rssi<info.devices[index].rssi))index=i;
            if((!heart_rate&&info.devices[index].heart_rate)||
               (heart_rate==info.devices[index].heart_rate&&report->rssi<=info.devices[index].rssi))return;
        }
        memset(&info.devices[index],0,sizeof(info.devices[index]));
        memcpy(info.devices[index].address,report->addr.val,6);info.devices[index].address_type=report->addr.type;
    }
    badge_ble_device_t *device=&info.devices[index];device->rssi=report->rssi;
    device->heart_rate|=heart_rate; /* ADV and scan response may carry different fields. */
    if(report->event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||report->event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)device->connectable=true;
    if(fields.name){
        size_t n=fields.name_len<32?fields.name_len:32;memcpy(device->name,fields.name,n);device->name[n]=0;
    }
}
static void hr_schedule_retry(void){
    info.heart_rate.has_measurement=false;
    if(!hr_wanted||!info.enabled)return;
    info.heart_rate.status=BADGE_HR_RECONNECTING;
    ble_npl_callout_reset(&hr_retry,ble_npl_time_ms_to_ticks32(hr_retry_ms));
    if(hr_retry_ms<30000)hr_retry_ms=hr_retry_ms>15000?30000:hr_retry_ms*2;
}
static void hr_stop(void){
    hr_wanted=false;hr_generation++;ble_npl_callout_stop(&hr_retry);
    info.heart_rate=(badge_heart_rate_t){.stale=true};
    hr_start=hr_end=hr_value=hr_value_end=hr_cccd=0;
}
static void hr_failure(int error){
    hr_generation++;info.heart_rate.error=error;info.heart_rate.has_measurement=false;
    info.heart_rate.status=BADGE_HR_ERROR;
    hr_value=0;
    if(outgoing!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(outgoing,BLE_ERR_REM_USER_CONN_TERM);
    else hr_schedule_retry();
    publish();
}
static bool hr_current(uint16_t conn,void *arg){
    return hr_wanted&&info.enabled&&conn==outgoing&&(uint32_t)(uintptr_t)arg==hr_generation;
}
static int hr_subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    (void)attr;if(!hr_current(conn,arg))return 0;
    if(error->status){hr_failure(error->status);return 0;}
    info.heart_rate.status=BADGE_HR_CONNECTED;info.heart_rate.error=0;hr_retry_ms=2000;
    ble_npl_callout_reset(&hr_retry,ble_npl_time_ms_to_ticks32(5000));publish();return 0;
}
static int hr_descriptor(uint16_t conn,const struct ble_gatt_error *error,uint16_t value,const struct ble_gatt_dsc *dsc,void *arg){
    if(!hr_current(conn,arg))return 0;
    if(!error->status){
        if(value==hr_value&&hr_uuid_is(&dsc->uuid.u,0x2902))hr_cccd=dsc->handle;
        return 0;
    }
    if(error->status!=BLE_HS_EDONE||!hr_cccd){hr_failure(error->status==BLE_HS_EDONE?BLE_HS_ENOENT:error->status);return 0;}
    const uint8_t notify[]={1,0};info.heart_rate.status=BADGE_HR_SUBSCRIBING;publish();
    int rc=ble_gattc_write_flat(conn,hr_cccd,notify,sizeof(notify),hr_subscribed,arg);if(rc)hr_failure(rc);return 0;
}
static int hr_characteristic(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_chr *chr,void *arg){
    if(!hr_current(conn,arg))return 0;
    if(!error->status){
        if(!hr_value&&hr_uuid_is(&chr->uuid.u,0x2a37)&&(chr->properties&BLE_GATT_CHR_PROP_NOTIFY)){
            hr_value=chr->val_handle;hr_value_end=hr_end;
        }else if(hr_value&&chr->def_handle>hr_value&&chr->def_handle<=hr_value_end)hr_value_end=chr->def_handle-1;
        return 0;
    }
    if(error->status!=BLE_HS_EDONE||!hr_value||hr_value_end<=hr_value){hr_failure(error->status==BLE_HS_EDONE?BLE_HS_ENOENT:error->status);return 0;}
    int rc=ble_gattc_disc_all_dscs(conn,hr_value,hr_value_end,hr_descriptor,arg);if(rc)hr_failure(rc);return 0;
}
static int hr_service(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_svc *svc,void *arg){
    if(!hr_current(conn,arg))return 0;
    if(!error->status){if(!hr_start){hr_start=svc->start_handle;hr_end=svc->end_handle;}return 0;}
    if(error->status!=BLE_HS_EDONE||!hr_start){hr_failure(error->status==BLE_HS_EDONE?BLE_HS_ENOENT:error->status);return 0;}
    int rc=ble_gattc_disc_all_chrs(conn,hr_start,hr_end,hr_characteristic,arg);if(rc)hr_failure(rc);return 0;
}
static int connect_target(const badge_ble_device_t *device){
    ble_addr_t peer={.type=device->address_type};memcpy(peer.val,device->address,6);
    snprintf(info.outbound_name,sizeof(info.outbound_name),"%s",device->name);address(info.outbound_address,peer.val);
    if(hr_wanted){
        hr_generation++;hr_start=hr_end=hr_value=hr_value_end=hr_cccd=0;info.heart_rate.has_measurement=false;
        snprintf(info.heart_rate.device_name,sizeof(info.heart_rate.device_name),"%s",device->name);
        address(info.heart_rate.device_address,peer.val);info.heart_rate.rssi=device->rssi;
    }
    int rc=ble_gap_connect(address_type,&peer,10000,NULL,gap_event,(void*)1);info.connecting=rc==0;
    if(rc&&hr_wanted){info.heart_rate.error=rc;hr_schedule_retry();}
    return rc;
}
static void hr_reconnect(struct ble_npl_event *event){
    (void)event;if(!hr_wanted||!info.enabled)return;
    if(outgoing!=BLE_HS_CONN_HANDLE_NONE&&info.heart_rate.status==BADGE_HR_CONNECTED){
        int8_t rssi;
        if(!ble_gap_conn_rssi(outgoing,&rssi)&&rssi!=info.heart_rate.rssi){info.heart_rate.rssi=rssi;publish();}
        ble_npl_callout_reset(&hr_retry,ble_npl_time_ms_to_ticks32(5000));return;
    }
    if(!synchronized||info.scanning||info.connecting||outgoing!=BLE_HS_CONN_HANDLE_NONE){
        ble_npl_callout_reset(&hr_retry,ble_npl_time_ms_to_ticks32(1000));return;
    }
    /* Reconnect only the selected address, never guess a different same-name band. */
    int rc=connect_target(&hr_target);info.error=rc;publish();
}
static void advertise(void){
    if(!synchronized||!info.enabled||incoming!=BLE_HS_CONN_HANDLE_NONE)return;
    if(ble_gap_adv_active()){info.advertising=true;return;}
    const char *name="CABadge";
    struct ble_hs_adv_fields fields={.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP,
        .name=(const uint8_t*)name,.name_len=strlen(name),.name_is_complete=1,
        .uuids128=(ble_uuid128_t*)&service_uuid,.num_uuids128=1,.uuids128_is_complete=1};
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    int rc=ble_gap_adv_set_fields(&fields);
    if(!rc)rc=ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    info.advertising=rc==0;if(rc)info.error=rc;
}
static int gap_event(struct ble_gap_event *event,void *arg){
    bool central=arg==(void*)1;
    switch(event->type){
    case BLE_GAP_EVENT_SUBSCRIBE:
        if(event->subscribe.conn_handle==incoming&&event->subscribe.attr_handle==status_value)subscribed=event->subscribe.cur_notify;
        return 0;
    case BLE_GAP_EVENT_DISC:
        remember_device(&event->disc);return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        info.scanning=false;
        for(int i=0;i<info.count;i++)for(int j=i+1;j<info.count;j++)if(info.devices[j].heart_rate>info.devices[i].heart_rate||
            (info.devices[j].heart_rate==info.devices[i].heart_rate&&info.devices[j].rssi>info.devices[i].rssi)){
            badge_ble_device_t swap=info.devices[i];info.devices[i]=info.devices[j];info.devices[j]=swap;
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if(central)info.connecting=false;else info.advertising=false;
        if(event->connect.status==0){
            uint16_t handle=event->connect.conn_handle;
            if(central)outgoing=handle;else incoming=handle;
            struct ble_gap_conn_desc desc;
            if(ble_gap_conn_find(handle,&desc)==0)address(central?info.outbound_address:info.inbound_address,desc.peer_id_addr.val);
            info.error=0;
            if(!info.enabled)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
            else if(central&&hr_wanted){
                info.heart_rate.status=BADGE_HR_DISCOVERING;
                int rc=ble_gattc_disc_svc_by_uuid(handle,&hr_service_uuid.u,hr_service,(void*)(uintptr_t)hr_generation);
                if(rc)hr_failure(rc);
            }
            else if(central&&info.heart_rate.status==BADGE_HR_IDLE&&hr_target.heart_rate)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        }else info.error=info.enabled?event->connect.status:0;
        if(central&&event->connect.status&&hr_wanted){info.heart_rate.error=event->connect.status;hr_schedule_retry();}
        advertise();break;
    case BLE_GAP_EVENT_DISCONNECT:
        management_ble_disconnect(event->disconnect.conn.conn_handle);
        if(event->disconnect.conn.conn_handle==incoming)subscribed=false;
        if(event->disconnect.conn.conn_handle==incoming){incoming=BLE_HS_CONN_HANDLE_NONE;info.inbound_address[0]=0;}
        if(event->disconnect.conn.conn_handle==outgoing){
            outgoing=BLE_HS_CONN_HANDLE_NONE;info.outbound_address[0]=info.outbound_name[0]=0;
            hr_generation++;hr_value=0;
            if(hr_wanted){if(!info.heart_rate.error)info.heart_rate.error=event->disconnect.reason;hr_schedule_retry();}
        }
        info.error=0;advertise();break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        info.advertising=false;advertise();break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if(event->notify_rx.conn_handle!=outgoing||event->notify_rx.attr_handle!=hr_value||!hr_wanted)return 0;
        {
            uint8_t data[3];uint16_t bpm;size_t n=OS_MBUF_PKTLEN(event->notify_rx.om);
            size_t copied=n<sizeof(data)?n:sizeof(data);
            if(os_mbuf_copydata(event->notify_rx.om,0,copied,data)||!badge_hr_parse(data,copied,&bpm))info.heart_rate.invalid_packets++;
            else {info.heart_rate.bpm=bpm;info.heart_rate.has_measurement=true;
                info.heart_rate.last_update_ms=(uint32_t)(esp_timer_get_time()/1000);info.heart_rate.notifications++;}
        }
        break;
    default:return 0;
    }
    publish();return 0;
}
static void apply_control(struct ble_npl_event *event){
    (void)event;command_t command;
    while(xQueueReceive(commands,&command,0)==pdTRUE){
        int rc=0;info.error=0;
        if(command.kind==ENABLE){
            info.enabled=command.flag;
            if(info.enabled)advertise();
            else {
                hr_stop();
                management_close();
                if(ble_gap_disc_active())ble_gap_disc_cancel();
                if(info.connecting)ble_gap_conn_cancel();
                info.scanning=false;info.connecting=false;info.count=0;
                if(ble_gap_adv_active())ble_gap_adv_stop();
                info.advertising=false;
                if(incoming!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(incoming,BLE_ERR_REM_USER_CONN_TERM);
                if(outgoing!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(outgoing,BLE_ERR_REM_USER_CONN_TERM);
            }
        }else if(command.kind==DISCONNECT){
            uint16_t handle=command.flag?outgoing:incoming;
            if(command.flag){hr_stop();if(info.connecting)ble_gap_conn_cancel();}
            if(handle!=BLE_HS_CONN_HANDLE_NONE)rc=ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        }else if(!info.enabled||!synchronized)rc=BLE_HS_EDISABLED;
        else if(command.kind==SCAN){
            if(info.connecting||info.scanning)rc=BLE_HS_EBUSY;
            else {
                struct ble_gap_disc_params params={.passive=0,.filter_duplicates=0,.itvl=160,.window=20};
                info.count=0;
                rc=ble_gap_disc(address_type,8000,&params,gap_event,(void*)2);info.scanning=rc==0;
            }
        }else if(command.kind==CONNECT){
            if(outgoing!=BLE_HS_CONN_HANDLE_NONE||info.connecting)rc=BLE_HS_EBUSY;
            else if(!command.device.connectable)rc=BLE_HS_EINVAL;
            else {
                if(ble_gap_disc_active())ble_gap_disc_cancel();
                info.scanning=false;
                hr_stop();hr_target=command.device;hr_wanted=command.device.heart_rate;hr_retry_ms=2000;
                if(hr_wanted)info.heart_rate.status=BADGE_HR_CONNECTING;
                rc=connect_target(&command.device);
            }
        }
        if(rc)info.error=rc;
        publish();
    }
}
static int request(command_t command){
    if(!initialized){badge_ui_ble_message("蓝牙初始化失败，请重启");return MG_FAILED;}
    if(xQueueSend(commands,&command,0)!=pdTRUE){badge_ui_ble_message("操作排队中，请稍后重试");return MG_BUSY;}
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&control);
    return MG_OK;
}
static void enable(bool on){if(!on)management_close();request((command_t){.kind=ENABLE,.flag=on});}
static void scan(void){request((command_t){.kind=SCAN});}
static void connect_device(const badge_ble_device_t *device){request((command_t){.kind=CONNECT,.device=*device});}
static void disconnect(bool outgoing_link){request((command_t){.kind=DISCONNECT,.flag=outgoing_link});}
static void synced(void){
    synchronized=ble_hs_id_infer_auto(0,&address_type)==0;
    if(synchronized){advertise();if(hr_wanted)hr_schedule_retry();}else info.error=BLE_HS_EUNKNOWN;publish();
}
static void reset(int reason){
    management_ble_disconnect(incoming);subscribed=false;
    synchronized=false;incoming=outgoing=BLE_HS_CONN_HANDLE_NONE;
    hr_generation++;hr_value=0;info.heart_rate.has_measurement=false;ble_npl_callout_stop(&hr_retry);
    if(hr_wanted)info.heart_rate.status=BADGE_HR_RECONNECTING;
    info.advertising=info.scanning=info.connecting=false;info.error=reason?reason:BLE_HS_EUNKNOWN;publish();
}
static void host_task(void *arg){(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
void ble_service_init(badge_state_t *value){
    state=value;state->ble_connected=-1;
    const badge_ble_actions_t actions={.enable=enable,.scan=scan,.connect=connect_device,.disconnect=disconnect};badge_ui_ble_bind(&actions);
    updates=xQueueCreate(1,sizeof(info));commands=xQueueCreate(8,sizeof(command_t));
    int rc=updates&&commands?nimble_port_init():ESP_ERR_NO_MEM;
    if(rc){info.enabled=false;info.error=rc;observed=info;badge_ui_ble_update(&info);return;}
    ble_svc_gap_init();ble_svc_gatt_init();
    if(ble_svc_gap_device_name_set("CABadge")!=0||ble_gatts_count_cfg(services)||ble_gatts_add_svcs(services)){
        nimble_port_deinit();info.error=BLE_HS_EUNKNOWN;badge_ui_ble_update(&info);return;
    }
    ble_npl_event_init(&control,apply_control,NULL);
    ble_npl_event_init(&notify_event,notify_status,NULL);
    if(ble_npl_callout_init(&hr_retry,nimble_port_get_dflt_eventq(),hr_reconnect,NULL)){
        nimble_port_deinit();info.enabled=false;info.error=ESP_ERR_NO_MEM;observed=info;badge_ui_ble_update(&info);return;
    }
    ble_hs_cfg.sync_cb=synced;ble_hs_cfg.reset_cb=reset;
    initialized=true;nimble_port_freertos_init(host_task);
}
void ble_service_poll(void){
    badge_ble_info_t current;
    bool changed=false,refresh=false;
    if(updates&&xQueueReceive(updates,&current,0)==pdTRUE){
        refresh=observed.enabled!=current.enabled||observed.advertising!=current.advertising||
            observed.scanning!=current.scanning||observed.connecting!=current.connecting||
            observed.inbound!=current.inbound||observed.outbound!=current.outbound||observed.error!=current.error;
        observed=current;
        state->ble_connected=current.inbound||current.outbound?1:current.error?-1:0;
        changed=true;
    }
    bool stale=!badge_hr_fresh(&observed.heart_rate,(uint32_t)(esp_timer_get_time()/1000));
    if(observed.heart_rate.stale!=stale){observed.heart_rate.stale=stale;changed=true;}
    if(changed)badge_ui_ble_update(&observed);
    if(refresh)badge_ui_refresh();
}
void ble_service_heart_rate_snapshot(badge_heart_rate_t *out){
    portENTER_CRITICAL(&hr_guard);*out=hr_observed;portEXIT_CRITICAL(&hr_guard);
    out->stale=!badge_hr_fresh(out,(uint32_t)(esp_timer_get_time()/1000));
    if(out->stale)out->bpm=0;
}
void ble_service_snapshot(badge_ble_info_t *out){*out=observed;if(!initialized){out->enabled=false;out->error=info.error?info.error:BLE_HS_EDISABLED;}}
int ble_service_control(const uint8_t *data,size_t n){
    (void)n;if(!initialized)return MG_FAILED;
    if(data[1]==MG_BLE_ENABLE){if(!data[2])management_close();return request((command_t){.kind=ENABLE,.flag=data[2]});}
    if(!observed.enabled)return MG_FAILED;
    if(data[1]==MG_BLE_DISCONNECT)return request((command_t){.kind=DISCONNECT,.flag=data[2]});
    if(observed.scanning||observed.connecting)return MG_BUSY;
    if(data[1]==MG_BLE_SCAN)return request((command_t){.kind=SCAN});
    if(data[1]==MG_BLE_CONNECT){
        if(observed.outbound)return MG_BUSY;
        for(int i=0;i<observed.count;i++){
            badge_ble_device_t *d=&observed.devices[i];
            if(d->address_type==data[2]&&!memcmp(d->address,data+3,6)&&d->connectable)return request((command_t){.kind=CONNECT,.device=*d});
        }
        return MG_INVALID;
    }
    return MG_INVALID;
}
