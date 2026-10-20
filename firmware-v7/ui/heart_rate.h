#ifndef CABADGE_HEART_RATE_H
#define CABADGE_HEART_RATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BADGE_HR_STALE_MS 30000u
enum { BADGE_HR_IDLE, BADGE_HR_CONNECTING, BADGE_HR_DISCOVERING,
       BADGE_HR_SUBSCRIBING, BADGE_HR_CONNECTED, BADGE_HR_RECONNECTING,
       BADGE_HR_ERROR };
typedef struct {
    int status,error,rssi;
    uint16_t bpm;
    bool has_measurement,stale;
    uint32_t last_update_ms,notifications,invalid_packets;
    char device_name[33],device_address[18];
} badge_heart_rate_t;

/* Standard 0x2A37: optional energy/RR fields do not change the BPM offset. */
static inline bool badge_hr_parse(const uint8_t *data,size_t n,uint16_t *bpm){
    if(!data||!bpm||n<2||((data[0]&1)&&n<3))return false;
    *bpm=(data[0]&1)?(uint16_t)(data[1]|((uint16_t)data[2]<<8)):data[1];
    return true;
}
static inline bool badge_hr_fresh(const badge_heart_rate_t *hr,uint32_t now){
    return hr->status==BADGE_HR_CONNECTED&&hr->has_measurement&&
           (uint32_t)(now-hr->last_update_ms)<BADGE_HR_STALE_MS;
}
static inline const char *badge_hr_status_name(int status){
    switch(status){
    case BADGE_HR_CONNECTING:return "Connecting";
    case BADGE_HR_DISCOVERING:return "Discovering";
    case BADGE_HR_SUBSCRIBING:return "Subscribing";
    case BADGE_HR_CONNECTED:return "Connected";
    case BADGE_HR_RECONNECTING:return "Reconnecting";
    case BADGE_HR_ERROR:return "Error";
    default:return "Disconnected";
    }
}
/* Any task may read a coherent copy. Stale/disconnected copies expose BPM=0;
 * always check stale before consuming it. Timestamp is monotonic uptime ms. */
void ble_service_heart_rate_snapshot(badge_heart_rate_t *out);
#endif
