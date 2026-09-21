/* GAP peripheral from the tested diagnostic, plus one outbound BLE link.
 * NimBLE owns radio state. Only the UI task calls LVGL. */
#include "ui/badge_ui.h"
#include "esp_err.h"
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
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(1,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static const ble_uuid128_t status_uuid=BLE_UUID128_INIT(2,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static const ble_uuid128_t control_uuid=BLE_UUID128_INIT(3,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
static const ble_uuid128_t session_uuid=BLE_UUID128_INIT(4,0,0,0,0,0,0,0x80,0,0x40,0x42,0x41,0x43,0x54,0x42,0x4e);
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
    if(kind==3)result=management_ble_authorize(conn,data,length);
    else if(!management_ble_allowed(conn,&generation))result=MG_UNAUTHORIZED;
    else result=management_submit(data,length,generation,conn,&id);
    memset(data,0,sizeof(data));
    return result==MG_OK||result==MG_ACCEPTED?0:result==MG_UNAUTHORIZED?BLE_ATT_ERR_INSUFFICIENT_AUTHOR:result==MG_INVALID?BLE_ATT_ERR_VALUE_NOT_ALLOWED:0x80+result;
}
static const struct ble_gatt_chr_def characteristics[]={
    {.uuid=&status_uuid.u,.access_cb=gatt_access,.arg=(void*)1,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&status_value},
    {.uuid=&control_uuid.u,.access_cb=gatt_access,.arg=(void*)2,.flags=BLE_GATT_CHR_F_WRITE},
    {.uuid=&session_uuid.u,.access_cb=gatt_access,.arg=(void*)3,.flags=BLE_GATT_CHR_F_WRITE},
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
    xQueueOverwrite(updates,&info);
}
static void address(char *text,const uint8_t *a){snprintf(text,18,"%02X:%02X:%02X:%02X:%02X:%02X",a[5],a[4],a[3],a[2],a[1],a[0]);}
static void remember_device(const struct ble_gap_disc_desc *report){
    if(!info.scanning)return;
    int index=-1;
    for(int i=0;i<info.count;i++)if(info.devices[i].address_type==report->addr.type&&!memcmp(info.devices[i].address,report->addr.val,6)){index=i;break;}
    if(index<0){
        if(info.count<BADGE_BLE_DEVICES)index=info.count++;
        else {
            index=0;for(int i=1;i<info.count;i++)if(info.devices[i].rssi<info.devices[index].rssi)index=i;
            if(report->rssi<=info.devices[index].rssi)return;
        }
        memset(&info.devices[index],0,sizeof(info.devices[index]));
        memcpy(info.devices[index].address,report->addr.val,6);info.devices[index].address_type=report->addr.type;
    }
    badge_ble_device_t *device=&info.devices[index];device->rssi=report->rssi;
    if(report->event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||report->event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)device->connectable=true;
    struct ble_hs_adv_fields fields;
    if(ble_hs_adv_parse_fields(&fields,report->data,report->length_data)==0&&fields.name){
        size_t n=fields.name_len<32?fields.name_len:32;memcpy(device->name,fields.name,n);device->name[n]=0;
    }
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
        for(int i=0;i<info.count;i++)for(int j=i+1;j<info.count;j++)if(info.devices[j].rssi>info.devices[i].rssi){
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
        }else info.error=info.enabled?event->connect.status:0;
        advertise();break;
    case BLE_GAP_EVENT_DISCONNECT:
        management_ble_disconnect(event->disconnect.conn.conn_handle);
        if(event->disconnect.conn.conn_handle==incoming)subscribed=false;
        if(event->disconnect.conn.conn_handle==incoming){incoming=BLE_HS_CONN_HANDLE_NONE;info.inbound_address[0]=0;}
        if(event->disconnect.conn.conn_handle==outgoing){outgoing=BLE_HS_CONN_HANDLE_NONE;info.outbound_address[0]=info.outbound_name[0]=0;}
        info.error=0;advertise();break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        info.advertising=false;advertise();break;
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
            if(handle!=BLE_HS_CONN_HANDLE_NONE)rc=ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        }else if(!info.enabled||!synchronized)rc=BLE_HS_EDISABLED;
        else if(command.kind==SCAN){
            if(info.connecting||info.scanning)rc=BLE_HS_EBUSY;
            else {
                struct ble_gap_disc_params params={.passive=0,.filter_duplicates=1,.itvl=160,.window=80};
                info.count=0;
                rc=ble_gap_disc(address_type,8000,&params,gap_event,(void*)2);info.scanning=rc==0;
            }
        }else if(command.kind==CONNECT){
            if(outgoing!=BLE_HS_CONN_HANDLE_NONE||info.connecting)rc=BLE_HS_EBUSY;
            else if(!command.device.connectable)rc=BLE_HS_EINVAL;
            else {
                if(ble_gap_disc_active())ble_gap_disc_cancel();
                info.scanning=false;
                ble_addr_t peer={.type=command.device.address_type};memcpy(peer.val,command.device.address,6);
                snprintf(info.outbound_name,sizeof(info.outbound_name),"%s",command.device.name);
                address(info.outbound_address,peer.val);
                rc=ble_gap_connect(address_type,&peer,15000,NULL,gap_event,(void*)1);info.connecting=rc==0;
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
    if(synchronized)advertise();else info.error=BLE_HS_EUNKNOWN;publish();
}
static void reset(int reason){
    management_ble_disconnect(incoming);subscribed=false;
    synchronized=false;incoming=outgoing=BLE_HS_CONN_HANDLE_NONE;
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
    ble_hs_cfg.sync_cb=synced;ble_hs_cfg.reset_cb=reset;
    initialized=true;nimble_port_freertos_init(host_task);
}
void ble_service_poll(void){
    badge_ble_info_t current;
    if(updates&&xQueueReceive(updates,&current,0)==pdTRUE){
        observed=current;
        state->ble_connected=current.inbound||current.outbound?1:current.error?-1:0;
        badge_ui_ble_update(&current);badge_ui_refresh();
    }
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
