#ifndef CABADGE_MANAGEMENT_PROTOCOL_H
#define CABADGE_MANAGEMENT_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define MG_VERSION 1
#define MG_MAX_COMMAND 100
#define MG_STATUS_BYTES 20
enum { MG_OK, MG_BUSY, MG_INVALID, MG_FAILED, MG_UNAUTHORIZED, MG_ACCEPTED };
enum { MG_WIFI_ENABLE=1, MG_WIFI_SCAN, MG_WIFI_CONNECT, MG_WIFI_DISCONNECT,
       MG_BLE_ENABLE, MG_BLE_SCAN, MG_BLE_CONNECT, MG_BLE_DISCONNECT,
       MG_BRIGHTNESS, MG_SLEEP, MG_REDUCED, MG_WALL_SELECT, MG_WALL_DELETE };
static inline int mg_unhex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
/* Length-delimited Wi-Fi text; no embedded NUL or control bytes. */
static inline bool mg_text(const uint8_t *p,size_t n,bool ascii){
    for(size_t i=0;i<n;i++){
        unsigned a=p[i];if(a<32||a==127||(ascii&&a>126))return false;
        if(a<128)continue;
        unsigned count=a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:0;
        if(!count||i+count>=n)return false;
        unsigned b=p[i+1];
        if((a==0xe0&&b<0xa0)||(a==0xed&&b>=0xa0)||(a==0xf0&&b<0x90)||(a==0xf4&&b>=0x90))return false;
        for(unsigned j=0;j<count;j++){if((p[++i]&0xc0)!=0x80)return false;}
    }
    return true;
}
static inline bool mg_valid_command(const uint8_t *p,size_t n){
    if(n<2||n>MG_MAX_COMMAND||p[0]!=MG_VERSION)return false;
    switch(p[1]){
    case MG_WIFI_SCAN:case MG_WIFI_DISCONNECT:case MG_BLE_SCAN:return n==2;
    case MG_WIFI_ENABLE:case MG_BLE_ENABLE:case MG_BLE_DISCONNECT:case MG_SLEEP:case MG_REDUCED:return n==3&&p[2]<=1;
    case MG_WALL_SELECT:return n==3&&p[2]<33;
    case MG_WALL_DELETE:return n==3&&p[2]>=2&&p[2]<33;
    case MG_BRIGHTNESS:return n==3&&p[2]>=10&&p[2]<=100;
    case MG_BLE_CONNECT:return n==9&&p[2]<=1; /* address type + six little-endian address bytes */
    case MG_WIFI_CONNECT:
        if(n<4||!p[2]||p[2]>32||p[3]>63||(p[3]&&p[3]<8)||n!=(size_t)4+p[2]+p[3])return false;
        return mg_text(p+4,p[2],false)&&mg_text(p+4+p[2],p[3],true);
    default:return false;
    }
}
#endif
