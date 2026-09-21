#ifndef CABADGE_BRIDGE_PROTOCOL_H
#define CABADGE_BRIDGE_PROTOCOL_H
#include "protocol.h"
#include "management_protocol.h"

#define BR_VERSION 1
#define BR_PCB 0xc7c59dffu
#define BR_CAP_SERVICES 1u
#define BR_CAP_WALL_READ 2u
#define BR_CAP_PERF 4
#define BR_CAP_LCD_PERF 8u
#define BR_CAP_AUDIT 16u
enum { BR_HELLO=32, BR_READY, BR_STATE, BR_COMMAND, BR_ACK, BR_WALL_READ, BR_WALL_DATA, BR_PERF, BR_PERF_RESULT };
enum { BR_APPLY=32, BR_MANAGEMENT, BR_HOTSPOT, BR_SHAKE };
/* Commands carry a request id followed by MG_VERSION, operation and arguments.
 * Local USB is the physical control path; HTTP/BLE authorization is unchanged. */
static inline bool br_command_valid(const uint8_t *p,size_t n){
    if(n<6||!jx_u32(p))return false;
    p+=4;n-=4;
    if(mg_valid_command(p,n))return true;
    if(n<2||p[0]!=MG_VERSION)return false;
    if(p[1]==BR_APPLY)return n==3&&p[2]<=2;
    return n==2&&(p[1]==BR_MANAGEMENT||p[1]==BR_HOTSPOT||p[1]==BR_SHAKE);
}
#endif
