#ifndef CABADGE_MANAGEMENT_H
#define CABADGE_MANAGEMENT_H
#include "management_protocol.h"
#include "ui/badge_ui.h"
void management_init(badge_state_t *state);
void management_poll(void);
void management_close(void);
bool management_begin_request(uint32_t *generation);
bool management_generation_valid(uint32_t generation);
int management_submit(const uint8_t *data,size_t n,uint32_t generation,uint16_t ble_handle,uint32_t *id);
bool management_json(char *out,size_t n);
bool management_ble_allowed(uint16_t handle,uint32_t *generation);
void management_ble_disconnect(uint16_t handle);
bool management_ble_status(uint16_t handle,uint8_t out[MG_STATUS_BYTES]);
/* Called only by the LVGL owner; services reuse the same action entrypoints. */
int wifi_service_control(const uint8_t *data,size_t n);
void wifi_service_snapshot(bool *scanning,bool *connecting,int *error,badge_wifi_ap_t *aps,int *count);
int ble_service_control(const uint8_t *data,size_t n);
void ble_service_snapshot(badge_ble_info_t *out);
void ble_service_status_changed(void);
#endif
