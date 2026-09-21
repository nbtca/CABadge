#include "bridge.h"
#include "usb_screen/bridge_protocol.h"
#include "usb_screen/wallpaper_store.h"
#include "src/misc/cache/lv_cache.h"
#include "cJSON.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE port=INVALID_HANDLE_VALUE;
static jx_parser_t parser;
static badge_state_t *s;
static bool ready;
static char status[128]="未连接设备";
static uint32_t sequence;
static uint64_t last_rx,last_poll,last_info;
static struct {uint32_t id;uint8_t op,arg;uint64_t at;} requests[16];
static badge_wifi_ap_t old_aps[12];static int old_count=-1;static badge_ble_info_t old_ble;
static lv_image_dsc_t picture;
static uint8_t *picture_data,*download;
static uint32_t generation,download_gen,download_crc,download_offset;
static bool download_wait;
static uint64_t download_at;
static struct {uint8_t *pixels;uint32_t size,session,offset;uint8_t waiting;uint64_t at;} upload;
static uint64_t now(void){return GetTickCount64();}
/* Overlapped serial I/O keeps a slow USB write out of LVGL's animation loop. */
static OVERLAPPED rx_io,tx_io;
static bool rx_pending,tx_pending;
static uint8_t rx_bytes[8192];
static struct {uint8_t bytes[JX_HEADER+JX_MAX_PAYLOAD];DWORD size,offset;} tx[8];
static unsigned tx_head,tx_tail;
static void pump_write(void){
    if(port==INVALID_HANDLE_VALUE)return;
    DWORD count=0;
    if(tx_pending){
        if(!GetOverlappedResult(port,&tx_io,&count,FALSE)){
            if(GetLastError()==ERROR_IO_INCOMPLETE)return;
            bridge_close();return;
        }
        tx_pending=false;
        if(!count){bridge_close();return;}
        tx[tx_head].offset+=count;
        if(tx[tx_head].offset==tx[tx_head].size){memset(&tx[tx_head],0,sizeof(tx[0]));tx_head=(tx_head+1)%8;}
    }
    if(tx_head==tx_tail)return;
    ResetEvent(tx_io.hEvent);
    if(!WriteFile(port,tx[tx_head].bytes+tx[tx_head].offset,tx[tx_head].size-tx[tx_head].offset,&count,&tx_io)&&GetLastError()!=ERROR_IO_PENDING){bridge_close();return;}
    tx_pending=true;
}
static bool send_packet(uint8_t type,const void *data,size_t size){
    if(port==INVALID_HANDLE_VALUE)return false;
    unsigned next=(tx_tail+1)%8;
    if(next==tx_head){bridge_close();snprintf(status,sizeof(status),"USB 写入超时");return false;}
    size_t n=jx_packet(tx[tx_tail].bytes,type,data,size);if(!n)return false;
    tx[tx_tail].size=(DWORD)n;tx[tx_tail].offset=0;tx_tail=next;pump_write();return port!=INVALID_HANDLE_VALUE;
}
static const cJSON *item(const cJSON *o,const char *name){return cJSON_GetObjectItemCaseSensitive(o,name);}
static bool flag(const cJSON *o,const char *name){return cJSON_IsTrue(item(o,name));}
static int number(const cJSON *o,const char *name,int fallback){const cJSON *v=item(o,name);return cJSON_IsNumber(v)?v->valueint:fallback;}
static uint32_t u32(const cJSON *o,const char *name){const cJSON *v=item(o,name);return cJSON_IsNumber(v)&&v->valuedouble>=0&&v->valuedouble<=4294967295.0?(uint32_t)v->valuedouble:0;}
static const char *string(const cJSON *o,const char *name){const cJSON *v=item(o,name);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
static void copy_name(char *out,size_t size,const char *value){
    size_t n=strlen(value);if(n>=size||!mg_text((const uint8_t*)value,n,false)){*out=0;return;}memcpy(out,value,n+1);
}
static void stop_upload(const char *message){free(upload.pixels);memset(&upload,0,sizeof(upload));badge_ui_wall_pending(false);if(message)badge_ui_notice(message);}
void bridge_close(void){
    if(port!=INVALID_HANDLE_VALUE){
        CancelIoEx(port,NULL);DWORD count;
        if(rx_pending)GetOverlappedResult(port,&rx_io,&count,TRUE);
        if(tx_pending)GetOverlappedResult(port,&tx_io,&count,TRUE);
        CloseHandle(port);
    }
    if(rx_io.hEvent)CloseHandle(rx_io.hEvent);if(tx_io.hEvent)CloseHandle(tx_io.hEvent);
    rx_io=(OVERLAPPED){0};tx_io=(OVERLAPPED){0};rx_pending=tx_pending=false;tx_head=tx_tail=0;memset(tx,0,sizeof(tx));
    port=INVALID_HANDLE_VALUE;ready=false;memset(&parser,0,sizeof(parser));memset(requests,0,sizeof(requests));
    free(download);download=NULL;download_wait=false;stop_upload(NULL);old_count=-1;memset(&old_ble,0,sizeof(old_ble));
    if(s){badge_ui_restore_photo(&badge_wallpaper);lv_image_cache_drop(&picture);free(picture_data);picture_data=NULL;generation=0;
        s->battery_mv=-1;s->wifi_connected=s->ble_connected=-1;s->wifi_rssi=-127;s->wifi_ssid[0]=0;s->wifi_enabled=false;
        badge_wifi_hide();badge_ui_ble_update(&old_ble);badge_ui_connected(false);badge_ui_wifi_results(NULL,0);badge_ui_wifi_message("未连接设备");
        badge_wallpaper_update("",false,"","","未连接设备");badge_management_update(false,"",0);}
    snprintf(status,sizeof(status),"未连接设备");
}
bool bridge_ready(void){return ready;}
const char *bridge_status(void){return status;}
static void command(uint8_t op,const void *args,size_t n){
    if(!ready){badge_ui_wall_pending(false);badge_ui_notice("请先连接设备");return;}
    int slot=-1;for(int i=0;i<16;i++)if(!requests[i].id){slot=i;break;}
    if(slot<0||n>96){badge_ui_notice("设备忙，请稍后重试");return;}
    uint8_t data[104]={0};if(!++sequence)sequence++;jx_put32(data,sequence);data[4]=MG_VERSION;data[5]=op;if(n)memcpy(data+6,args,n);
    if(!br_command_valid(data,6+n)){badge_ui_notice("参数不符合要求");memset(data,0,sizeof(data));return;}
    requests[slot].id=sequence;requests[slot].op=op;requests[slot].arg=n?data[6]:0;requests[slot].at=now();
    send_packet(BR_COMMAND,data,6+n);memset(data,0,sizeof(data));
}
static void scan(void){command(MG_WIFI_SCAN,NULL,0);}
static void wifi_enable(bool on){uint8_t v=on;command(MG_WIFI_ENABLE,&v,1);}
static void connect_wifi(const char *ssid,const char *password){
    size_t a=strlen(ssid),b=strlen(password);if(a>32||b>63)return;
    uint8_t data[97]={(uint8_t)a,(uint8_t)b};memcpy(data+2,ssid,a);memcpy(data+2+a,password,b);command(MG_WIFI_CONNECT,data,a+b+2);memset(data,0,sizeof(data));
}
static void ble_enable(bool on){uint8_t v=on;command(MG_BLE_ENABLE,&v,1);}
static void ble_scan(void){command(MG_BLE_SCAN,NULL,0);}
static void ble_connect(const badge_ble_device_t *d){uint8_t data[7]={d->address_type};memcpy(data+1,d->address,6);command(MG_BLE_CONNECT,data,7);}
static void ble_disconnect(bool outgoing){uint8_t value=outgoing;command(MG_BLE_DISCONNECT,&value,1);}
static void manage(void){command(BR_MANAGEMENT,NULL,0);}
static void hotspot(void){command(BR_HOTSPOT,NULL,0);}
void bridge_shake(void){command(BR_SHAKE,NULL,0);}
static void control(int op,int value){uint8_t v=value;command(op==BADGE_APPLY?BR_APPLY:op==BADGE_BRIGHTNESS?MG_BRIGHTNESS:op==BADGE_REDUCED?MG_REDUCED:MG_SLEEP,&v,1);}
void bridge_init(badge_state_t *state){
    s=state;
    badge_wifi_actions_t wifi={scan,connect_wifi,wifi_enable};badge_ui_wifi_bind(&wifi);
    badge_ble_actions_t ble={ble_enable,ble_scan,ble_connect,ble_disconnect};badge_ui_ble_bind(&ble);
    badge_ui_control_bind(control);badge_management_bind(manage);badge_wallpaper_bind(hotspot);bridge_close();
}
bool bridge_snapshot(const char *json,size_t n){
    if(n>8191||!n||memchr(json,0,n))return false;
    const char *end=NULL;cJSON *root=cJSON_ParseWithLengthOpts(json,n,&end,false);
    if(!root)return false;
    while(end<json+n&&(*end==' '||*end=='\r'||*end=='\n'||*end=='\t'))end++;
    if(end!=json+n){cJSON_Delete(root);return false;}
    const cJSON *wifi=item(root,"wifi"),*ble=item(root,"ble"),*wall=item(root,"wallpaper");
    int brightness=number(root,"brightness",-1),chosen=number(wall,"selected",-1);
    if(!cJSON_IsObject(wifi)||!cJSON_IsObject(ble)||!cJSON_IsObject(wall)||number(root,"protocol",0)!=1||strncmp(string(root,"firmware"),"7.",2)||brightness<10||brightness>100||chosen<0||chosen>2){cJSON_Delete(root);return false;}
    s->battery_mv=number(root,"battery_mv",-1);s->brightness=brightness;s->reduced_motion=flag(root,"reduced_motion");s->wallpaper_index=chosen;
    s->wifi_enabled=flag(wifi,"enabled");s->wifi_connected=flag(wifi,"connected");s->wifi_rssi=number(wifi,"rssi",-127);copy_name(s->wifi_ssid,sizeof(s->wifi_ssid),string(wifi,"ssid"));
    badge_wifi_ap_t aps[12]={0};int count=0;
    const cJSON *list=item(wifi,"networks");
    for(const cJSON *ap=cJSON_IsArray(list)?list->child:NULL;ap&&count<12;ap=ap->next){
        copy_name(aps[count].ssid,sizeof(aps[count].ssid),string(ap,"ssid"));aps[count].rssi=number(ap,"rssi",-127);aps[count].security=number(ap,"security",2);count++;
    }
    if(count!=old_count||memcmp(old_aps,aps,sizeof(aps))){memcpy(old_aps,aps,sizeof(aps));old_count=count;badge_ui_wifi_results(aps,count);}
    badge_ui_wifi_message(flag(wifi,"scanning")?"正在扫描…":flag(wifi,"connecting")?"正在连接…":number(wifi,"error",0)?"连接失败，请检查密码或重试":s->wifi_connected?"已连接":s->wifi_enabled?"附近的网络":"Wi-Fi 已关闭");
    badge_ble_info_t info={.enabled=flag(ble,"enabled"),.advertising=flag(ble,"advertising"),.scanning=flag(ble,"scanning"),.connecting=flag(ble,"connecting"),.error=number(ble,"error",0)};
    copy_name(info.inbound_address,sizeof(info.inbound_address),string(ble,"incoming"));copy_name(info.outbound_address,sizeof(info.outbound_address),string(ble,"outgoing"));info.inbound=*info.inbound_address!=0;info.outbound=*info.outbound_address!=0;
    list=item(ble,"devices");
    for(const cJSON *d=cJSON_IsArray(list)?list->child:NULL;d&&info.count<12;d=d->next){
        const char *addr=string(d,"address");if(strlen(addr)!=12)continue;
        badge_ble_device_t *v=&info.devices[info.count];bool valid=true;
        for(int i=0;i<6;i++){int a=mg_unhex(addr[2*i]),b=mg_unhex(addr[2*i+1]);if(a<0||b<0){valid=false;break;}v->address[i]=(a<<4)|b;}if(!valid)continue;
        copy_name(v->name,sizeof(v->name),string(d,"name"));v->address_type=number(d,"type",0);v->rssi=number(d,"rssi",-127);v->connectable=flag(d,"connectable");info.count++;
    }
    s->ble_connected=info.inbound||info.outbound;
    if(memcmp(&old_ble,&info,sizeof(info))){old_ble=info;badge_ui_ble_update(&info);}
    bool asleep=flag(root,"asleep");if(badge_ui_is_asleep()!=asleep)badge_ui_sleep(asleep);
    uint32_t gen=u32(wall,"generation"),size=u32(wall,"size"),crc=u32(wall,"crc");
    bool resource=cJSON_IsNumber(item(wall,"generation"))&&cJSON_IsNumber(item(wall,"size"))&&cJSON_IsNumber(item(wall,"crc"));
    if(resource&&size==0&&(picture_data||generation!=gen)){badge_ui_restore_photo(&badge_wallpaper);lv_image_cache_drop(&picture);free(picture_data);picture_data=NULL;generation=gen;}
    if(resource&&size==WALL_BYTES&&(!picture_data||generation!=gen)&&(!download||download_gen!=gen)&&!upload.pixels){
        free(download);download=malloc(WALL_BYTES);download_gen=gen;download_crc=crc;download_offset=0;download_wait=false;
        badge_ui_wall_pending(true);if(!download)badge_ui_notice("无法分配壁纸缓存");
    }
    badge_ui_refresh();cJSON_Delete(root);return true;
}
static void upload_next(void){
    uint8_t data[4104];jx_put32(data,upload.session);
    if(upload.offset==upload.size){upload.waiting=JX_WALL_FINISH;send_packet(JX_WALL_FINISH,data,4);}
    else {size_t n=upload.size-upload.offset;if(n>4096)n=4096;jx_put32(data+4,upload.offset);memcpy(data+8,upload.pixels+upload.offset,n);upload.waiting=JX_WALL_CHUNK;send_packet(JX_WALL_CHUNK,data,n+8);}
    upload.at=now();
}
static void received(uint8_t type,const uint8_t *data,size_t n,void *ctx){
    (void)ctx;
    if(type==BR_READY){
        if(n!=12||data[0]!=BR_VERSION||data[1]!=16||jx_u16(data+2)!=360||jx_u16(data+4)!=360||jx_u32(data+8)!=BR_PCB||(jx_u16(data+6)&3)!=3){bridge_close();snprintf(status,sizeof(status),"固件不支持 v7 实板联调");return;}
        ready=true;last_rx=now();generation=0;badge_ui_connected(true);snprintf(status,sizeof(status),"实板已连接 · PC LVGL");return;
    }
    if(!ready)return;
    if(type==BR_STATE){if(bridge_snapshot((const char*)data,n))last_rx=now();return;}
    if(type==BR_ACK&&n==5){
        uint32_t id=jx_u32(data);for(int i=0;i<16;i++)if(requests[i].id==id){
            uint8_t op=requests[i].op,arg=requests[i].arg;requests[i].id=0;
            if(data[4]!=MG_OK){if(op==BR_APPLY)badge_ui_apply_result(false);badge_ui_wall_pending(false);badge_ui_notice(data[4]==MG_BUSY?"设备忙，请稍后重试":"操作失败，请重试");}
            else if(op==BR_APPLY){s->wallpaper_index=arg;badge_ui_wall_pending(false);badge_ui_refresh();badge_ui_apply_result(true);}
            else if(op==MG_SLEEP)badge_ui_sleep(arg);
            else if(op==MG_BRIGHTNESS){s->brightness=arg;badge_ui_refresh();}
            else if(op==MG_REDUCED){s->reduced_motion=arg;badge_ui_refresh();}
            break;
        }return;
    }
    if(type==JX_WALL_INFO&&n<600){
        cJSON *v=cJSON_ParseWithLength((const char*)data,n);if(!v)return;
        char url[128]="";const char *key=string(v,"key"),*ip=string(v,"ip");bool ap=flag(v,"hotspot");
        if(flag(v,"authorized")&&flag(v,"http")&&strlen(key)==32&&(ap||*ip))snprintf(url,sizeof(url),"http://%s/#key=%s",ap?"192.168.4.1":ip,key);
        badge_wallpaper_update(url,ap,string(v,"ssid"),string(v,"password"),"");badge_management_update(flag(v,"authorized"),key,number(v,"remaining",0));cJSON_Delete(v);return;
    }
    if(type==BR_WALL_DATA&&download&&download_wait&&n>=9){
        if(jx_u32(data)!=download_gen||jx_u32(data+4)!=download_offset)return;
        download_wait=false;
        if(data[8]||n==9||n-9>WALL_BYTES-download_offset){free(download);download=NULL;return;}
        memcpy(download+download_offset,data+9,n-9);download_offset+=(uint32_t)n-9;
        if(download_offset==WALL_BYTES){
            if(~jx_crc(~0u,download,WALL_BYTES)!=download_crc){free(download);download=NULL;badge_ui_notice("壁纸校验失败");return;}
            lv_image_cache_drop(&picture);uint8_t *old=picture_data;picture_data=download;download=NULL;generation=download_gen;
            picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=WALL_BYTES,.data=picture_data};
            badge_ui_restore_photo(&picture);free(old);badge_ui_wall_pending(false);
        }return;
    }
    if(type==JX_WALL_ACK&&upload.pixels&&n==10){
        if(data[0]!=upload.waiting)return;
        if(data[0]!=JX_WALL_BEGIN&&jx_u32(data+2)!=upload.session)return;
        if(data[1]){stop_upload("壁纸上传失败");return;}
        if(data[0]==JX_WALL_BEGIN){upload.session=jx_u32(data+2);upload.offset=0;}
        else if(data[0]==JX_WALL_CHUNK){uint32_t expected=upload.offset+((upload.size-upload.offset)>4096?4096:upload.size-upload.offset);if(jx_u32(data+6)!=expected){stop_upload("上传位置不一致");return;}upload.offset=expected;}
        else if(data[0]==JX_WALL_FINISH){stop_upload("已保存，正在同步");return;}
        upload_next();
    }
}
bool bridge_open(const char *name){
    bridge_close();int num=0;char tail=0;if(sscanf(name,"COM%d%c",&num,&tail)!=1||num<1||num>256)return false;
    char path[24];snprintf(path,sizeof(path),"\\\\.\\COM%d",num);port=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if(port==INVALID_HANDLE_VALUE){snprintf(status,sizeof(status),"串口无法打开或已被占用");return false;}
    DCB d={.DCBlength=sizeof(d)};COMMTIMEOUTS t={.ReadIntervalTimeout=MAXDWORD,.WriteTotalTimeoutConstant=1000};
    if(!GetCommState(port,&d)){bridge_close();return false;}
    d.BaudRate=115200;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;d.fBinary=TRUE;d.fOutxCtsFlow=d.fOutxDsrFlow=d.fOutX=d.fInX=FALSE;d.fDtrControl=DTR_CONTROL_DISABLE;d.fRtsControl=RTS_CONTROL_DISABLE;
    if(!SetCommState(port,&d)||!SetCommTimeouts(port,&t)){bridge_close();return false;}
    rx_io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);tx_io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!rx_io.hEvent||!tx_io.hEvent){bridge_close();return false;}
    PurgeComm(port,PURGE_RXCLEAR|PURGE_TXCLEAR);uint8_t version=BR_VERSION;last_rx=now();last_poll=last_info=0;
    snprintf(status,sizeof(status),"正在连接 v7 固件…");return send_packet(BR_HELLO,&version,1);
}
void bridge_poll(void){
    if(port==INVALID_HANDLE_VALUE)return;pump_write();if(port==INVALID_HANDLE_VALUE)return;
    DWORD n=0;bool available=false;
    if(rx_pending){
        if(GetOverlappedResult(port,&rx_io,&n,FALSE)){rx_pending=false;available=true;}
        else if(GetLastError()!=ERROR_IO_INCOMPLETE){bridge_close();return;}
    }else{
        ResetEvent(rx_io.hEvent);
        if(ReadFile(port,rx_bytes,sizeof(rx_bytes),&n,&rx_io))available=true;
        else if(GetLastError()==ERROR_IO_PENDING)rx_pending=true;
        else {bridge_close();return;}
    }
    if(available&&n)jx_feed(&parser,rx_bytes,n,received,NULL);
    if(port==INVALID_HANDLE_VALUE)return;
    uint64_t t=now();if(t-last_rx>5000){bridge_close();snprintf(status,sizeof(status),"连接超时，请确认已安装 v7 固件");return;}
    if(!ready)return;
    if(t-last_poll>=250){last_poll=t;send_packet(BR_STATE,NULL,0);}
    if(t-last_info>=1000){last_info=t;send_packet(JX_WALL_INFO,NULL,0);}
    for(int i=0;i<16;i++)if(requests[i].id&&t-requests[i].at>4000){requests[i].id=0;badge_ui_wall_pending(false);badge_ui_notice("回执超时，正在核对状态");}
    if(upload.pixels&&t-upload.at>15000){uint8_t data[4];jx_put32(data,upload.session);send_packet(JX_WALL_CANCEL,data,4);stop_upload("上传超时，结果未确认");}
    if(download&&download_wait&&t-download_at>2000)download_wait=false;
    if(download&&!download_wait&&!upload.pixels){uint8_t p[10];jx_put32(p,download_gen);jx_put32(p+4,download_offset);jx_put16(p+8,(uint16_t)(WALL_BYTES-download_offset>4096?4096:WALL_BYTES-download_offset));download_wait=true;download_at=t;send_packet(BR_WALL_READ,p,10);}
}
bool bridge_upload(const uint8_t *pixels,size_t size){
    if(!ready||size!=WALL_BYTES||upload.pixels)return false;
    free(download);download=NULL;download_wait=false;upload.pixels=malloc(size);if(!upload.pixels)return false;memcpy(upload.pixels,pixels,size);upload.size=size;
    uint8_t p[8];jx_put32(p,(uint32_t)size);jx_put32(p+4,~jx_crc(~0u,pixels,size));upload.waiting=JX_WALL_BEGIN;upload.at=now();badge_ui_wall_pending(true);badge_ui_notice("正在上传…");return send_packet(JX_WALL_BEGIN,p,8);
}

/* Exercise Windows overlapped I/O and the production parser without hardware. */
static void check_packet(uint8_t type,const uint8_t *data,size_t n,void *ctx){
    unsigned *seen=ctx;
    if(type==BR_HELLO&&n==1&&data[0]==BR_VERSION)*seen|=1;
    if(type==BR_COMMAND&&br_command_valid(data,n)&&data[5]==BR_APPLY)*seen|=2;
}
bool bridge_transport_check(void){
    bridge_close();char name[100];snprintf(name,sizeof(name),"\\\\.\\pipe\\CABadge-v7-check-%lu",(unsigned long)GetCurrentProcessId());
    HANDLE server=CreateNamedPipeA(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,65536,65536,0,NULL);
    if(server==INVALID_HANDLE_VALUE)return false;
    bool ok=false;DWORD n=0;unsigned seen=0;jx_parser_t remote={0};uint8_t bytes[JX_HEADER+JX_MAX_PAYLOAD];
    port=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if(port==INVALID_HANDLE_VALUE)goto end;
    if(!ConnectNamedPipe(server,NULL)&&GetLastError()!=ERROR_PIPE_CONNECTED)goto end;
    rx_io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);tx_io.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!rx_io.hEvent||!tx_io.hEvent)goto end;
    last_rx=now();uint8_t version=BR_VERSION;if(!send_packet(BR_HELLO,&version,1))goto end;
    for(int i=0;i<10;i++){bridge_poll();Sleep(1);}
    DWORD available=0;if(!PeekNamedPipe(server,NULL,0,NULL,&available,NULL)||!available)goto end;
    if(!ReadFile(server,bytes,sizeof(bytes),&n,NULL))goto end;jx_feed(&remote,bytes,n,check_packet,&seen);
    if(!(seen&1))goto end;
    uint8_t hello[12]={BR_VERSION,16};jx_put16(hello+2,360);jx_put16(hello+4,360);jx_put16(hello+6,3);jx_put32(hello+8,BR_PCB);
    size_t size=jx_packet(bytes,BR_READY,hello,12);
    if(!WriteFile(server,bytes,5,&n,NULL))goto end;
    for(int i=0;i<5;i++){bridge_poll();Sleep(1);}if(ready)goto end;
    if(!WriteFile(server,bytes+5,(DWORD)size-5,&n,NULL))goto end;
    for(int i=0;i<10&&!ready;i++){bridge_poll();Sleep(1);}if(!ready)goto end;
    int before=s->wallpaper_index;uint8_t chosen=1;command(BR_APPLY,&chosen,1);uint32_t id=sequence;
    for(int i=0;i<10;i++){bridge_poll();Sleep(1);}
    if(!PeekNamedPipe(server,NULL,0,NULL,&available,NULL)||!available)goto end;
    if(!ReadFile(server,bytes,sizeof(bytes),&n,NULL))goto end;jx_feed(&remote,bytes,n,check_packet,&seen);if(!(seen&2))goto end;
    uint8_t ack[5];jx_put32(ack,id);ack[4]=MG_FAILED;size=jx_packet(bytes,BR_ACK,ack,5);
    if(!WriteFile(server,bytes,(DWORD)size,&n,NULL))goto end;
    for(int i=0;i<10;i++){bridge_poll();Sleep(1);}if(s->wallpaper_index!=before)goto end;
    for(int i=0;i<16;i++)if(requests[i].id==id)goto end;
    hello[1]=32;size=jx_packet(bytes,BR_READY,hello,12);if(!WriteFile(server,bytes,(DWORD)size,&n,NULL))goto end;
    for(int i=0;i<10&&ready;i++){bridge_poll();Sleep(1);}
    ok=!ready&&port==INVALID_HANDLE_VALUE;
end:
    bridge_close();CloseHandle(server);return ok;
}
