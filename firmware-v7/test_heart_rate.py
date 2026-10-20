"""Compile production HRS/GAP/state functions with mocked NimBLE/RTOS boundaries.
This checks protocol/lifecycle logic, not real radio or LVGL visual acceptance.
"""
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/heart-rate-test')
out.mkdir(parents=True, exist_ok=True)
source = (root/'usb_screen/device/src/ble_service.c').read_text(encoding='utf-8')
header = (root/'ui/badge_ui.h').read_text(encoding='utf-8')


def function(name):
    start = re.search(r'^\w[^\n]*\b'+name+r'\([^;\n]*\)\s*\{', source, re.M).start()
    opening = source.index('{', start)
    depth = 1
    end = opening+1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]+'\n'


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "heart_rate.h"
#include "host/ble_uuid.h"
#define BADGE_BLE_DEVICES 12
typedef struct {int ble_connected;} badge_state_t;
typedef void *QueueHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int locked;
#define portENTER_CRITICAL(p) do {assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(p) do {assert(locked);locked=0;}while(0)
#define pdTRUE 1
#define BLE_HS_CONN_HANDLE_NONE 0xffff
enum { BLE_HS_EDONE=14,BLE_HS_ENOENT=5,BLE_HS_EBUSY=15,BLE_HS_EINVAL=3,BLE_HS_EDISABLED=30,
 BLE_ERR_REM_USER_CONN_TERM=19,BLE_HCI_ADV_RPT_EVTYPE_ADV_IND=0,BLE_HCI_ADV_RPT_EVTYPE_DIR_IND=1,
 BLE_GAP_EVENT_SUBSCRIBE=3,BLE_GAP_EVENT_DISC,BLE_GAP_EVENT_DISC_COMPLETE,BLE_GAP_EVENT_CONNECT,
 BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_ADV_COMPLETE,BLE_GAP_EVENT_NOTIFY_RX,BLE_GATT_CHR_PROP_NOTIFY=0x10 };
typedef struct {uint8_t type,val[6];} ble_addr_t;
struct os_mbuf {uint8_t data[8];unsigned len;};
#define OS_MBUF_PKTLEN(om) ((om)->len)
struct ble_npl_event {int dummy;};
struct ble_npl_callout {unsigned delay;};
struct ble_gap_conn_desc {uint16_t conn_handle;ble_addr_t peer_id_addr;};
struct ble_gap_disc_desc {ble_addr_t addr;int8_t rssi;uint8_t event_type;const uint8_t *data;uint8_t length_data;};
struct ble_gap_disc_params {uint8_t passive,filter_duplicates;uint16_t itvl,window;};
struct ble_gap_event {int type;union {
 struct {uint16_t conn_handle,attr_handle;bool cur_notify;} subscribe;
 struct ble_gap_disc_desc disc;
 struct {int status;uint16_t conn_handle;} connect;
 struct {struct ble_gap_conn_desc conn;int reason;} disconnect;
 struct {uint16_t conn_handle,attr_handle;struct os_mbuf *om;} notify_rx;
};};
struct ble_hs_adv_fields {const ble_uuid16_t *uuids16;uint8_t num_uuids16;const ble_uuid32_t *uuids32;uint8_t num_uuids32;
 const ble_uuid128_t *uuids128;uint8_t num_uuids128;const uint8_t *svc_data_uuid16;uint8_t svc_data_uuid16_len;
 const uint8_t *name;uint8_t name_len;};
struct ble_gatt_error {int status;};
struct ble_gatt_svc {uint16_t start_handle,end_handle;ble_uuid_any_t uuid;};
struct ble_gatt_chr {uint16_t def_handle,val_handle;uint8_t properties;ble_uuid_any_t uuid;};
struct ble_gatt_dsc {uint16_t handle;ble_uuid_any_t uuid;};
struct ble_gatt_attr {int dummy;};
static uint32_t now_ms;static int gui_calls,refresh_calls,stage,terminated,canceled,rc_next;
static bool scan_active,queued_update;
static uint16_t request_start,request_end;static void *callback_arg;
static struct ble_hs_adv_fields adv_fields;
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
int ble_uuid_cmp(const ble_uuid_t *a,const ble_uuid_t *b){
 if(a->type!=b->type)return a->type-b->type;
 return a->type==16?(int)BLE_UUID16(a)->value-BLE_UUID16(b)->value:memcmp(BLE_UUID128(a)->value,BLE_UUID128(b)->value,16);
}
static int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *f,const uint8_t *p,uint8_t n){*f=adv_fields;return 0;}
static int os_mbuf_copydata(struct os_mbuf *om,int offset,size_t n,void *p){if(offset+n>om->len)return 1;memcpy(p,om->data+offset,n);return 0;}
static unsigned ble_npl_time_ms_to_ticks32(unsigned ms){return ms;}
static void ble_npl_callout_stop(struct ble_npl_callout *co){co->delay=0;}
static void ble_npl_callout_reset(struct ble_npl_callout *co,unsigned ticks){co->delay=ticks;}
static void management_close(void){}
static void management_ble_disconnect(uint16_t conn){}
static bool ble_gap_disc_active(void){return scan_active;}
static bool ble_gap_adv_active(void){return false;}
static int ble_gap_adv_stop(void){return 0;}
static int ble_gap_disc_cancel(void){scan_active=false;return 0;}
static int ble_gap_conn_cancel(void){canceled++;return 0;}
static int result(void){int rc=rc_next;rc_next=0;return rc;}
static int ble_gap_terminate(uint16_t conn,int reason){terminated++;return 0;}
static int ble_gap_conn_rssi(uint16_t conn,int8_t *rssi){*rssi=-55;return 0;}
static int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc *desc){memset(desc,0,sizeof(*desc));return 0;}
static int ble_gap_connect(uint8_t type,const ble_addr_t *addr,int duration,void *params,int(*cb)(struct ble_gap_event*,void*),void *arg){stage=1;assert(duration<=10000);return result();}
static int ble_gap_disc(uint8_t type,int duration,const struct ble_gap_disc_params *params,int(*cb)(struct ble_gap_event*,void*),void *arg){assert(duration==8000&&params->window==20&&!params->filter_duplicates);scan_active=true;return result();}
static int ble_gattc_disc_svc_by_uuid(uint16_t conn,const ble_uuid_t *uuid,int(*cb)(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_svc*,void*),void *arg){assert(BLE_UUID16(uuid)->value==0x180d);stage=2;callback_arg=arg;return result();}
static int ble_gattc_disc_all_chrs(uint16_t conn,uint16_t start,uint16_t end,int(*cb)(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_chr*,void*),void *arg){stage=3;request_start=start;request_end=end;return result();}
static int ble_gattc_disc_all_dscs(uint16_t conn,uint16_t start,uint16_t end,int(*cb)(uint16_t,const struct ble_gatt_error*,uint16_t,const struct ble_gatt_dsc*,void*),void *arg){stage=4;request_start=start;request_end=end;return result();}
static int ble_gattc_write_flat(uint16_t conn,uint16_t handle,const void *data,uint16_t n,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){assert(handle==23&&n==2&&!memcmp(data,"\x01\x00",2));stage=5;return result();}
'''
for name in ('badge_ble_device_t', 'badge_ble_info_t'):
    code += re.search(r'typedef struct \{[^{}]*\} '+name+';', header, re.S)[0]+'\n'
code += 'static badge_ble_info_t latest,shown;\n'
code += source[source.index('static badge_state_t *state;'):source.index('static const ble_uuid128_t service_uuid=')]
code += source[source.index('enum { ENABLE='):source.index('static void publish(void)')]
code += r'''
static void advertise(void){info.advertising=synchronized&&info.enabled&&incoming==BLE_HS_CONN_HANDLE_NONE;}
static void xQueueOverwrite(QueueHandle_t q,const void *p){latest=*(const badge_ble_info_t*)p;queued_update=true;}
static command_t pending_command;static bool has_command;
static int xQueueReceive(QueueHandle_t q,void *p,int wait){
 if(q==commands){if(!has_command)return 0;*(command_t*)p=pending_command;has_command=false;return 1;}
 if(!queued_update)return 0;*(badge_ble_info_t*)p=latest;queued_update=false;return 1;
}
static void badge_ui_ble_update(const badge_ble_info_t *i){shown=*i;gui_calls++;}
static void badge_ui_refresh(void){gui_calls++;refresh_calls++;}
'''
for name in ('publish', 'address', 'hr_uuid_is', 'advertises_heart_rate', 'remember_device',
             'hr_schedule_retry', 'hr_stop', 'hr_failure', 'hr_current', 'hr_subscribed',
             'hr_descriptor', 'hr_characteristic', 'hr_service', 'connect_target',
             'hr_reconnect', 'gap_event', 'apply_control', 'ble_service_poll', 'ble_service_heart_rate_snapshot'):
    code += function(name)
code += r'''
static const struct ble_gatt_error ok={0},done={BLE_HS_EDONE};
static void command(command_t c){pending_command=c;has_command=true;apply_control(NULL);}
static void notify(uint16_t conn,uint16_t attr,const uint8_t *bytes,size_t n){
 struct os_mbuf om={.len=n};memcpy(om.data,bytes,n);
 struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_RX,.notify_rx={conn,attr,&om}};gap_event(&e,(void*)1);
}
static void discovery(void *obsolete){
 struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={0,9}};gap_event(&e,(void*)1);
 assert(stage==2&&info.heart_rate.status==BADGE_HR_DISCOVERING);
 if(obsolete){hr_subscribed(9,&ok,NULL,obsolete);hr_service(9,&done,NULL,obsolete);assert(stage==2&&info.heart_rate.status==BADGE_HR_DISCOVERING);}
 struct ble_gatt_svc svc={.start_handle=20,.end_handle=40};hr_service(9,&ok,&svc,callback_arg);hr_service(9,&done,NULL,callback_arg);
 assert(stage==3&&request_start==20&&request_end==40);
 struct ble_gatt_chr chr={.def_handle=21,.val_handle=22,.properties=BLE_GATT_CHR_PROP_NOTIFY,.uuid.u16=BLE_UUID16_INIT(0x2a37)};
 hr_characteristic(9,&ok,&chr,callback_arg);
 chr=(struct ble_gatt_chr){.def_handle=26,.val_handle=27,.uuid.u16=BLE_UUID16_INIT(0x2a38)};hr_characteristic(9,&ok,&chr,callback_arg);
 hr_characteristic(9,&done,NULL,callback_arg);assert(stage==4&&request_start==22&&request_end==25);
 struct ble_gatt_dsc dsc={.handle=23,.uuid.u16=BLE_UUID16_INIT(0x2902)};
 hr_descriptor(9,&ok,22,&dsc,callback_arg);hr_descriptor(9,&done,22,NULL,callback_arg);
 assert(stage==5&&info.heart_rate.status==BADGE_HR_SUBSCRIBING);
 hr_subscribed(9,&ok,NULL,callback_arg);assert(info.heart_rate.status==BADGE_HR_CONNECTED&&hr_retry.delay==5000);
}
int main(void){
 uint16_t bpm=777;uint8_t u8[]={0,83},u16[]={1,0x34,0x12},optional[]={0x18,94,1,2,3,4};
 assert(badge_hr_parse(u8,2,&bpm)&&bpm==83);assert(badge_hr_parse(u16,3,&bpm)&&bpm==0x1234);
 assert(badge_hr_parse(optional,6,&bpm)&&bpm==94);bpm=777;
 assert(!badge_hr_parse(NULL,0,&bpm)&&!badge_hr_parse(u8,1,&bpm)&&!badge_hr_parse(u16,2,&bpm)&&bpm==777);
 badge_heart_rate_t hr={.status=BADGE_HR_CONNECTED,.has_measurement=true,.last_update_ms=UINT32_MAX-100};
 assert(badge_hr_fresh(&hr,99));assert(!badge_hr_fresh(&hr,29900));hr.has_measurement=false;assert(!badge_hr_fresh(&hr,99));
 const ble_uuid16_t uuid16=BLE_UUID16_INIT(0x180d);const ble_uuid32_t uuid32=BLE_UUID32_INIT(0x180d);
 const ble_uuid128_t uuid128=BLE_UUID128_INIT(0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x0d,0x18,0,0);
 assert(hr_uuid_is(&uuid16.u,0x180d)&&hr_uuid_is(&uuid32.u,0x180d)&&hr_uuid_is(&uuid128.u,0x180d)&&!hr_uuid_is(&uuid128.u,0x2a37));
 badge_state_t shared={0};state=&shared;updates=(void*)1;commands=(void*)2;synchronized=true;now_ms=1000;
 command((command_t){.kind=SCAN});assert(info.scanning);
 struct ble_gap_disc_desc report={.addr={.type=1,.val={1,2,3,4,5,6}},.rssi=-60,.event_type=0};
 adv_fields=(struct ble_hs_adv_fields){.uuids16=&uuid16,.num_uuids16=1};remember_device(&report);
 adv_fields=(struct ble_hs_adv_fields){.name=(const uint8_t*)"Xiaomi Smart Band 9 Pro",.name_len=sizeof("Xiaomi Smart Band 9 Pro")-1};remember_device(&report);
 assert(info.count==1&&info.devices[0].heart_rate&&info.devices[0].connectable&&!strcmp(info.devices[0].name,"Xiaomi Smart Band 9 Pro"));
 for(int i=2;i<20;i++){report.addr.val[0]=i;report.rssi=-30-i;adv_fields=(struct ble_hs_adv_fields){0};remember_device(&report);}
 assert(info.count==12&&info.devices[0].heart_rate);
 struct ble_gap_event end={.type=BLE_GAP_EVENT_DISC_COMPLETE};gap_event(&end,(void*)2);scan_active=false;
 assert(info.devices[0].heart_rate);command((command_t){.kind=CONNECT,.device=info.devices[0]});assert(info.connecting&&hr_wanted&&stage==1);
 discovery(NULL);assert(!gui_calls);notify(9,22,u8,2);assert(info.heart_rate.notifications==1&&info.heart_rate.bpm==83);
 ble_service_heart_rate_snapshot(&hr);assert(!hr.stale&&hr.bpm==83&&!gui_calls);
 notify(8,22,u16,3);notify(9,99,u16,3);assert(info.heart_rate.notifications==1);
 now_ms=2000;notify(9,22,u16,2);assert(info.heart_rate.invalid_packets==1&&info.heart_rate.last_update_ms==1000);
 notify(9,22,u16,3);ble_service_poll();assert(gui_calls&&shown.heart_rate.bpm==0x1234);
 int previous_refreshes=refresh_calls;now_ms=2001;notify(9,22,u8,2);ble_service_poll();assert(refresh_calls==previous_refreshes&&shown.heart_rate.bpm==83);
 now_ms=32001;ble_service_poll();ble_service_heart_rate_snapshot(&hr);assert(shown.heart_rate.stale&&hr.stale&&hr.bpm==0&&refresh_calls==previous_refreshes);
 notify(9,22,u8,2);ble_service_heart_rate_snapshot(&hr);assert(!hr.stale&&hr.bpm==83);
 hr_reconnect(NULL);assert(info.heart_rate.rssi==-55&&hr_retry.delay==5000);
 void *old_arg=callback_arg;struct ble_gap_event drop={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn.conn_handle=9,.reason=8}};
 gap_event(&drop,(void*)1);ble_service_heart_rate_snapshot(&hr);assert(hr.stale&&hr.bpm==0&&hr.status==BADGE_HR_RECONNECTING&&hr_retry.delay==2000);
 hr_reconnect(NULL);assert(stage==1&&info.connecting);discovery(old_arg);
 hr_subscribed(9,&ok,NULL,old_arg);assert(info.heart_rate.status==BADGE_HR_CONNECTED&&!info.heart_rate.has_measurement);
 notify(9,22,u8,2);command((command_t){.kind=DISCONNECT,.flag=true});assert(!hr_wanted&&!hr_retry.delay&&terminated);
 gap_event(&drop,(void*)1);hr_reconnect(NULL);ble_service_heart_rate_snapshot(&hr);assert(hr.status==BADGE_HR_IDLE&&hr.stale&&hr.bpm==0);
 command((command_t){.kind=CONNECT,.device=info.devices[0]});old_arg=(void*)(uintptr_t)hr_generation;
 command((command_t){.kind=ENABLE,.flag=false});assert(!hr_wanted&&!hr_retry.delay&&canceled);
 struct ble_gap_event late={.type=BLE_GAP_EVENT_CONNECT,.connect={0,9}};gap_event(&late,(void*)1);
 assert(!hr_wanted&&info.heart_rate.status==BADGE_HR_IDLE);hr_service(9,&done,NULL,old_arg);assert(!hr_retry.delay);
 outgoing=BLE_HS_CONN_HANDLE_NONE;info.connecting=false;info.enabled=true;
 command((command_t){.kind=CONNECT,.device=info.devices[0]});late.connect.conn_handle=9;gap_event(&late,(void*)1);
 hr_service(9,&done,NULL,callback_arg);assert(info.heart_rate.status==BADGE_HR_ERROR&&!info.heart_rate.has_measurement);
 gap_event(&drop,(void*)1);assert(hr_retry.delay==2000);rc_next=BLE_HS_EBUSY;hr_reconnect(NULL);assert(hr_retry.delay==4000&&!info.connecting);
 for(int i=0;i<10;i++){rc_next=BLE_HS_EBUSY;hr_reconnect(NULL);}assert(hr_retry.delay==30000&&hr_retry_ms==30000);
 puts("PASS production HR: 8/16-bit/optional/truncation, UUID16/32/128, bounded scan merge/priority, GATT/CCCD, Notify isolation, stale/recovery/tick wrap, reconnect/cancel/disable/error/late callbacks; no host-to-LVGL call");
 printf("State sizes: HR=%zu bytes, BLE=%zu bytes\n",sizeof(badge_heart_rate_t),sizeof(badge_ble_info_t));
}
'''
(out/'check.c').write_text(code, encoding='utf-8')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp', ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
sdk = Path('F:/CABadgeBuild/platformio/idf61/packages/framework-espidf/components/bt/host/nimble/nimble/nimble/host/include')
exe = out/'check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG',
                '-I'+str(root/'ui'), '-I'+str(sdk), str(out/'check.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
