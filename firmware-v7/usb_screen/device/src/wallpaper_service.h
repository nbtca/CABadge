#ifndef JX_WALLPAPER_SERVICE_H
#define JX_WALLPAPER_SERVICE_H
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
void wallpaper_service_init(void);
int wallpaper_select(int id,bool remove);
int wallpaper_catalog(int ids[31]);
void wallpaper_service_poll(void);
/* 0=OK, 1=busy, 2=invalid, 3=storage failure, 4=session/offset error. */
int wallpaper_begin(int owner,uint32_t size,uint32_t crc,uint32_t *session);
int wallpaper_chunk(int owner,uint32_t session,uint32_t offset,const uint8_t *data,size_t n);
int wallpaper_finish(int owner,uint32_t session);
void wallpaper_cancel(int owner,uint32_t session);
void wallpaper_info(char *json,size_t n);
bool wallpaper_busy(void);
bool wallpaper_resource_info(uint32_t *generation,uint32_t *size,uint32_t *crc);
void wallpaper_status(int *phase,int *error,uint32_t *received);
void wallpaper_stop_hotspot(void);
int wallpaper_finish_async(uint32_t session);
bool wallpaper_finish_result(uint32_t *session,int *result);
esp_err_t wifi_wallpaper_hotspot(bool on,const char *name,const char *password);
void wifi_wallpaper_address(char *ip,size_t n);
bool wifi_wallpaper_hotspot_active(void);
#endif
