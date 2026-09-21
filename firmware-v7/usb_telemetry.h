#ifndef USB_TELEMETRY_H
#define USB_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int battery_mv;       /* -1 unknown */
    int wifi_connected;   /* -1 unknown, 0 disconnected, 1 connected */
    int ble_connected;    /* -1 unknown, 0 disconnected, 1 connected */
    int wifi_rssi;        /* -127 unknown */
    int prompt;           /* 0 none, 1 SSID, 2 password; no credentials stored */
    unsigned updates;
} usb_sample_t;

void usb_sample_reset(usb_sample_t *sample);
bool usb_feed_line(usb_sample_t *sample, const char *line);
int usb_ports(char *options, size_t capacity);
bool usb_open(const char *port, char *error, size_t capacity);
void usb_close(void);
bool usb_is_open(void);
const char *usb_error(void);
/* -1: transport failed/closed; 0: unchanged; 1: new parsed events or open reset. */
int usb_poll(usb_sample_t *sample);

#endif
