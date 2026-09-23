#ifndef BADGE_UI_H
#define BADGE_UI_H
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
void badge_ui_connection_cache_stats(char *out,size_t size);
/* Gallery UI; the action blue comes from the existing association mark. */
#define UI_BG 0xF4F6F8
#define UI_SURFACE 0xFFFFFF
#define UI_TEXT 0x1B2430
#define UI_MUTED 0x5B6778
#define UI_ACCENT 0x365B86
#define UI_SELECTED 0xDEE8F5
#define UI_LINE 0xDCE3EC
#define UI_VERSION "7.9.2-memory"
typedef struct {
    int brightness;
    bool reduced_motion;
    int wallpaper_index; /* 0 default, 1 ribbons, 2..32 persistent library IDs; persisted selection */
    int battery_mv;
    int wifi_connected; /* -1 unknown, 0 disconnected, 1 connected */
    int ble_connected;
    int wifi_rssi;
    bool live;
    bool wifi_enabled;
    char wifi_ssid[33];
} badge_state_t;
typedef struct { char ssid[33]; int rssi; unsigned security; bool saved; } badge_wifi_ap_t; /* 0=open,1=personal,2=enterprise */
typedef struct {
    void (*scan)(void);
    void (*connect)(const char *ssid,const char *password);
    void (*enable)(bool enabled);
} badge_wifi_actions_t;
#define BADGE_BLE_DEVICES 12
typedef struct {
    char name[33];
    uint8_t address[6],address_type;
    int rssi;
    bool connectable;
} badge_ble_device_t;
typedef struct {
    bool enabled,advertising,scanning,connecting,inbound,outbound;
    int error,count;
    char inbound_address[18],outbound_address[18],outbound_name[33];
    badge_ble_device_t devices[BADGE_BLE_DEVICES];
} badge_ble_info_t;
typedef struct {
    void (*enable)(bool enabled);
    void (*scan)(void);
    void (*connect)(const badge_ble_device_t *device);
    void (*disconnect)(bool outgoing);
} badge_ble_actions_t;
extern const lv_font_t font14, font18, font24, font36, font56;
extern const lv_image_dsc_t badge_logo, badge_wallpaper, badge_ribbons;
void badge_ui_click_guard(lv_event_t *event);
void badge_ui_create(lv_obj_t *parent, badge_state_t *state, void (*save)(void));
void badge_ui_page(int index, bool animate);
int badge_ui_current_page(void);
int badge_ui_selected_wallpaper(void);
void badge_ui_refresh(void);
/* Same widgets use direct board services or the PC USB adapter. */
enum { BADGE_APPLY=1, BADGE_BRIGHTNESS, BADGE_REDUCED, BADGE_SLEEP, BADGE_DELETE };
void badge_ui_control_bind(void (*callback)(int operation,int value));
void badge_ui_request(int operation,int value);
bool badge_ui_select_wallpaper(int index);
void badge_ui_notice(const char *message);
void badge_ui_connected(bool connected);
void badge_ui_wall_pending(bool pending);
void badge_ui_external_panel(lv_obj_t *panel,bool open);
void badge_ui_test_state(int *page,int *drawer,int *selection,float *turn);
void badge_ui_settings(int brightness,bool reduced);
int badge_ui_brightness(void);
void badge_management_bind(void (*callback)(void));
void badge_management_toggle(void);
void badge_management_update(bool active,const char *code,unsigned remaining);
void badge_ui_motion(void);
void badge_ui_sleep(bool sleep);
bool badge_ui_is_asleep(void);
bool badge_ui_gallery_clean(void);
void badge_ui_set_photo(const lv_image_dsc_t *photo);
void badge_ui_restore_photo(const lv_image_dsc_t *photo);
void badge_ui_library(const int *ids,const lv_image_dsc_t *const *thumbs,int count);
unsigned badge_ui_thumbnail_window(int ids[3]);
void badge_ui_thumbnails_changed(void);
void badge_ui_loaded_photo(const lv_image_dsc_t *photo,int id,bool apply_now);
void badge_ui_wifi_bind(const badge_wifi_actions_t *actions);
void badge_ui_wifi_results(const badge_wifi_ap_t *aps,int count);
void badge_ui_wifi_message(const char *message);
void badge_ui_wifi_text(const char *text);
void badge_ui_wifi_key(int key); /* 8=backspace,13=submit,27=cancel */
bool badge_ui_wifi_visible(void);
void badge_wifi_create(lv_obj_t *parent,badge_state_t *state);
void badge_wifi_open(void);
void badge_wifi_hide(void);
void badge_wifi_refresh(void);
void badge_ui_ble_bind(const badge_ble_actions_t *actions);
void badge_ui_ble_update(const badge_ble_info_t *info);
void badge_ui_ble_message(const char *message);
void badge_ble_create(lv_obj_t *parent,badge_state_t *state);
void badge_ble_open(void);
void badge_ble_hide(void);
bool badge_ui_ble_visible(void);
const char *badge_ble_summary(void);
void badge_wallpaper_create(lv_obj_t *parent);
void badge_wallpaper_open(void);
void badge_wallpaper_hide(void);
void badge_wallpaper_bind(void (*hotspot_toggle)(void));
void badge_wallpaper_hotspot(void);
void badge_wallpaper_update(const char *url,bool hotspot,const char *name,const char *password,const char *message);
lv_obj_t *badge_liquid_create(lv_obj_t*,int,int,lv_event_cb_t);
void badge_liquid_set(lv_obj_t*,bool,bool);
void badge_ui_apply_result(bool);
void badge_ui_probe_scene(int,unsigned);
void badge_ui_probe_restore(int);
bool badge_ui_perf_enabled(void);
void badge_ui_perf_enable(bool enabled);
void badge_ui_perf_text(const char *text);
bool badge_ui_perf_covers(const lv_area_t *area);

#endif

bool badge_ui_transition_active(void);

void badge_ui_probe_period(unsigned ms);

bool badge_ui_direct_panel(lv_obj_t *panel,int x);
void badge_ui_direct_panel_end(void);

void badge_ui_storage(uint32_t total_bytes,uint32_t available_bytes,unsigned free_slots,bool valid);
