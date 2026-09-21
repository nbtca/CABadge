#include "usb_telemetry.h"
#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE serial_port = INVALID_HANDLE_VALUE;
static char last_error[160], line_buffer[512];
static size_t line_used;
static bool discard_line, reset_pending;

void usb_sample_reset(usb_sample_t *sample)
{
    *sample = (usb_sample_t){ .battery_mv = -1, .wifi_connected = -1,
        .ble_connected = -1, .wifi_rssi = -127 };
}

static bool literal(const char **p, const char *expected)
{
    size_t length = strlen(expected);
    if (strncmp(*p, expected, length)) return false;
    *p += length;
    return true;
}

static bool number(const char **p, long minimum, long maximum, int *value)
{
    const char *start = *p;
    if (*start == '-' && minimum < 0) ++start;
    if (*start < '0' || *start > '9') return false;
    char *end;
    errno = 0;
    long result = strtol(*p, &end, 10);
    if (errno || result < minimum || result > maximum) return false;
    *p = end;
    if (value) *value = (int)result;
    return true;
}

static bool ended(const char *p)
{
    if (*p == '\r') ++p;
    if (*p == '\n') ++p;
    return !*p;
}

static bool ipv4(const char **p)
{
    for (int part = 0; part < 4; ++part) {
        if (!number(p, 0, 255, NULL)) return false;
        if (part != 3 && !literal(p, ".")) return false;
    }
    return true;
}

bool usb_feed_line(usb_sample_t *sample, const char *line)
{
    const char *p = line;
    int value, raw;
    unsigned updates = sample->updates;
    if (!strncmp(line, "ESP-ROM:esp32s3-", 16) ||
        !strcmp(line, "JXBadge Wi-Fi + BLE r1 diagnostic / PCB c7c59dff") ||
        !strcmp(line, "JXBadge headless r1 diagnostic / PCB c7c59dff") ||
        !strcmp(line, "JXBadge stage-4 diagnostic / PCB c7c59dff") ||
        !strcmp(line, "JXBadge stage-3 diagnostic / PCB c7c59dff")) {
        usb_sample_reset(sample);
    } else if (literal(&p, "BAT sample=")) {
        if (!number(&p, 0, INT_MAX, NULL) || !literal(&p, " raw=") ||
            !number(&p, -1, 4095, &raw) || !literal(&p, " voltage=")) return false;
        if (literal(&p, "UNAVAILABLE error=")) {
            if (!*p) return false;
            sample->battery_mv = -1;
        } else {
            /* Accept over/undervoltage readings too; do not clamp to UI slider limits. */
            if (raw < 0 || !number(&p, 0, 6000, &value) || !literal(&p, " mV")) return false;
            if (!ended(p) && strcmp(p, " (battery must be connected)")) return false;
            sample->battery_mv = value;
        }
    } else if (literal(&p, "WIFI LINK=UP RSSI=")) {
        if (!number(&p, -126, 0, &value) || !literal(&p, " dBm") || !ended(p)) return false;
        sample->wifi_connected = 1;
        sample->wifi_rssi = value;
        sample->prompt = 0;
    } else if (!strcmp(line, "WIFI LINK=DOWN; RESET to retry") || !strcmp(line, "WIFI LINK=DOWN")) {
        sample->wifi_connected = 0;
        sample->wifi_rssi = -127;
    } else if ((p = line, literal(&p, "WIFI CONNECTED IP="))) {
        if (!ipv4(&p) || !literal(&p, " GATEWAY=") || !ipv4(&p) || !ended(p)) return false;
        sample->wifi_connected = 1;
        sample->wifi_rssi = -127;
        sample->prompt = 0;
    } else if ((p = line, literal(&p, "WIFI DISCONNECTED reason="))) {
        if (!number(&p, 0, 65535, NULL) || !ended(p)) return false;
        sample->wifi_connected = 0;
        sample->wifi_rssi = -127;
    } else if ((p = line, literal(&p, "BLE CONNECT status="))) {
        if (!number(&p, 0, 65535, &value) ||
            (!ended(p) && strcmp(p, " (0=connected)"))) return false;
        sample->ble_connected = value == 0;
    } else if ((p = line, literal(&p, "BLE DISCONNECTED reason="))) {
        if (!number(&p, 0, 65535, NULL) || !ended(p)) return false;
        sample->ble_connected = 0;
    } else if (!strcmp(line, "ENTER SSID")) {
        sample->prompt = 1;
    } else if (!strcmp(line, "ENTER PASSWORD")) {
        sample->prompt = 2;
    } else if ((p = line, literal(&p, "WIFI connecting; timeout="))) {
        if (!number(&p, 1, 3600, NULL) || !literal(&p, "s") || !ended(p)) return false;
        sample->prompt = 0;
    } else return false;
    sample->updates = updates + 1;
    return true;
}

static bool feed_bytes(usb_sample_t *sample, const char *bytes, size_t length)
{
    bool changed = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\n') {
            if (!discard_line) {
                line_buffer[line_used] = 0;
                changed |= usb_feed_line(sample, line_buffer);
            }
            line_used = 0;
            discard_line = false;
        } else if (ch != '\r' && !discard_line) {
            if (!ch || line_used == sizeof(line_buffer) - 1) discard_line = true;
            else line_buffer[line_used++] = (char)ch;
        }
    }
    return changed;
}

int usb_ports(char *options, size_t capacity)
{
    if (!options || !capacity) return 0;
    options[0] = 0;
    size_t used = 0;
    int count = 0;
    for (int i = 1; i <= 256; ++i) {
        char port[16], target[512];
        snprintf(port, sizeof(port), "COM%d", i);
        if (!QueryDosDeviceA(port, target, sizeof(target)) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;
        size_t length = strlen(port);
        if (used + length + (count ? 1 : 0) >= capacity) break;
        if (count) options[used++] = '\n';
        memcpy(options + used, port, length + 1);
        used += length;
        ++count;
    }
    return count;
}

void usb_close(void)
{
    if (serial_port != INVALID_HANDLE_VALUE) CloseHandle(serial_port);
    serial_port = INVALID_HANDLE_VALUE;
    line_used = 0;
    discard_line = reset_pending = false;
}

bool usb_is_open(void) { return serial_port != INVALID_HANDLE_VALUE; }
const char *usb_error(void) { return last_error; }

static bool fail(const char *operation, DWORD code, char *error, size_t capacity)
{
    snprintf(last_error, sizeof(last_error), "%s (Windows error %lu)", operation, (unsigned long)code);
    if (error && capacity) snprintf(error, capacity, "%s", last_error);
    usb_close();
    return false;
}

bool usb_open(const char *port, char *error, size_t capacity)
{
    usb_close();
    last_error[0] = 0;
    if (error && capacity) error[0] = 0;
    const char *p = port ? port : "";
    int port_number;
    if (!literal(&p, "COM") || !number(&p, 1, 256, &port_number) || *p)
        return fail("Invalid COM port", ERROR_INVALID_NAME, error, capacity);
    char canonical[16];
    snprintf(canonical, sizeof(canonical), "COM%d", port_number);
    if (strcmp(port, canonical)) return fail("Invalid COM port", ERROR_INVALID_NAME, error, capacity);
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    serial_port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (!usb_is_open()) return fail("Cannot open port; close other serial monitors", GetLastError(), error, capacity);
    DCB config = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(serial_port, &config)) return fail("Cannot read serial settings", GetLastError(), error, capacity);
    config.BaudRate = CBR_115200;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = config.fOutxCtsFlow = config.fOutxDsrFlow = config.fDsrSensitivity = FALSE;
    config.fDtrControl = DTR_CONTROL_DISABLE;
    config.fRtsControl = RTS_CONTROL_DISABLE;
    config.fOutX = config.fInX = config.fErrorChar = config.fNull = config.fAbortOnError = FALSE;
    config.fTXContinueOnXoff = TRUE;
    if (!SetCommState(serial_port, &config)) return fail("Cannot configure serial port", GetLastError(), error, capacity);
    COMMTIMEOUTS timeouts = { .ReadIntervalTimeout = MAXDWORD };
    if (!SetCommTimeouts(serial_port, &timeouts)) return fail("Cannot configure nonblocking read", GetLastError(), error, capacity);
    reset_pending = true;
    return true;
}

static int usb_read_bytes(uint8_t *buffer,size_t capacity)
{
    if (!usb_is_open()||!capacity) return 0;
    DWORD errors, received;
    COMSTAT status;
    if (!ClearCommError(serial_port, &errors, &status)) {
        fail("USB connection lost", GetLastError(), NULL, 0);
        return -1;
    }
    if (errors & (CE_OVERRUN | CE_RXOVER | CE_FRAME | CE_RXPARITY | CE_BREAK)) {
        fail("Serial data error; reconnect", ERROR_INVALID_DATA, NULL, 0);
        return -1;
    }
    DWORD requested = status.cbInQue < capacity ? status.cbInQue : (DWORD)capacity;
    if (!requested) return 0;
    if (!ReadFile(serial_port, buffer, requested, &received, NULL)) {
        fail("USB read failed", GetLastError(), NULL, 0);
        return -1;
    }
    return (int)received;
}

int usb_poll(usb_sample_t *sample)
{
    if(!usb_is_open())return 0;
    bool changed=reset_pending;
    if(reset_pending){usb_sample_reset(sample);reset_pending=false;}
    uint8_t buffer[2048];int received=usb_read_bytes(buffer,sizeof(buffer));
    if(received<0){usb_sample_reset(sample);return -1;}
    changed |= feed_bytes(sample,(const char*)buffer,(size_t)received);
    return changed;
}

#ifdef USB_TELEMETRY_TEST
#define CHECK(expr) do { if(!(expr)) { fprintf(stderr,"CHECK failed: %s (%s:%d)\n",#expr,__FILE__,__LINE__); exit(1); } } while(0)

int main(void)
{
    usb_sample_t sample;
    usb_sample_reset(&sample);
    CHECK(sample.battery_mv == -1 && sample.wifi_connected == -1 && sample.ble_connected == -1 && sample.wifi_rssi == -127);
    CHECK(usb_feed_line(&sample, "BAT sample=79 raw=2986 voltage=4026 mV (battery must be connected)"));
    CHECK(sample.battery_mv == 4026 && sample.updates == 1);
    CHECK(usb_feed_line(&sample, "WIFI CONNECTED IP=10.11.180.139 GATEWAY=10.11.180.51"));
    CHECK(sample.wifi_connected == 1 && sample.wifi_rssi == -127);
    CHECK(usb_feed_line(&sample, "WIFI LINK=UP RSSI=-35 dBm"));
    CHECK(sample.wifi_rssi == -35);
    CHECK(usb_feed_line(&sample, "BLE CONNECT status=0 (0=connected)"));
    CHECK(sample.ble_connected == 1 && sample.wifi_connected == 1);
    CHECK(usb_feed_line(&sample, "BLE DISCONNECTED reason=531"));
    CHECK(sample.ble_connected == 0);
    CHECK(usb_feed_line(&sample, "BLE CONNECT status=0 (0=connected)"));
    CHECK(sample.ble_connected == 1);
    CHECK(usb_feed_line(&sample, "WIFI DISCONNECTED reason=201"));
    CHECK(sample.wifi_connected == 0 && sample.wifi_rssi == -127 && sample.ble_connected == 1);
    CHECK(usb_feed_line(&sample, "WIFI LINK=UP RSSI=-40 dBm"));
    CHECK(usb_feed_line(&sample, "WIFI LINK=DOWN; RESET to retry"));
    CHECK(sample.wifi_connected == 0 && sample.wifi_rssi == -127);
    CHECK(usb_feed_line(&sample, "ENTER SSID") && sample.prompt == 1);
    CHECK(usb_feed_line(&sample, "ENTER PASSWORD") && sample.prompt == 2);
    CHECK(usb_feed_line(&sample, "WIFI connecting; timeout=30s") && sample.prompt == 0);
    CHECK(usb_feed_line(&sample, "BAT sample=80 raw=-1 voltage=UNAVAILABLE error=ESP_FAIL") && sample.battery_mv == -1);
    CHECK(usb_feed_line(&sample, "ESP-ROM:esp32s3-20210327"));
    CHECK(sample.battery_mv == -1 && sample.wifi_connected == -1 && sample.ble_connected == -1 && sample.prompt == 0);
    unsigned before = sample.updates;
    const char *invalid[] = {
        "BLE advertising name=JXBadge-Test status=0 (0=OK)", "SSID=private", "password=private", "HEADLESS ALIVE sample=79 | BL=OFF CHG_ALLOW=LOW",
        "BAT sample=1 raw=2980 voltage=-1 mV", "BAT sample=1 raw=2980 voltage=6001 mV", "BAT sample=1 raw=4096 voltage=4030 mV",
        "BAT sample=1 raw=1 voltage=9999999999999999999999 mV", "BAT sample=1 raw=1 voltage=4.03 mV", "BAT sample=1 raw=1 voltage=4030junk mV", "BAT sample=1 raw=-1 voltage=4030 mV",
        "WIFI LINK=UP RSSI=-127 dBm", "WIFI LINK=UP RSSI=1 dBm", "WIFI LINK=UP RSSI=-35garbage dBm", "WIFI LINK=DOWNjunk",
        "BLE CONNECT status=-1", "BLE CONNECT status=0oops", "BLE CONNECT status=99999999999999", "BLE DISCONNECTED reason=abc",
        "WIFI CONNECTED IP= GATEWAY=", "WIFI CONNECTED IP=999.1.1.1 GATEWAY=1.1.1.1", "WIFI connecting; timeout=30things"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) CHECK(!usb_feed_line(&sample, invalid[i]));
    CHECK(sample.updates == before && sample.wifi_connected == -1 && sample.ble_connected == -1);
    const char first[] = "BAT sample=1 raw=2980 voltage=40";
    const char rest[] = "32 mV\r\nWIFI LINK=UP RSSI=-36 dBm\r\n";
    CHECK(!feed_bytes(&sample, first, strlen(first)));
    CHECK(feed_bytes(&sample, rest, strlen(rest)));
    CHECK(sample.battery_mv == 4032 && sample.wifi_rssi == -36);
    char overlong[600]; memset(overlong, 'x', sizeof(overlong));
    CHECK(!feed_bytes(&sample, overlong, sizeof(overlong)));
    const char recovery[] = "BLE CONNECT status=0\nBLE DISCONNECTED reason=531\n";
    CHECK(feed_bytes(&sample, recovery, strlen(recovery)) && sample.ble_connected == 0);
    CHECK(usb_feed_line(&sample, "JXBadge Wi-Fi + BLE r1 diagnostic / PCB c7c59dff"));
    CHECK(sample.battery_mv == -1 && sample.wifi_connected == -1 && sample.ble_connected == -1);
    char error[160];
    CHECK(!usb_open("COM3\\other", error, sizeof(error)) && !usb_is_open() && *error);
    CHECK(!usb_open("COM0003", error, sizeof(error)) && !usb_is_open() && *error);
    usb_close();
    CHECK(usb_poll(&sample) == 0);
    puts("USB telemetry parser: PASS (logs, bounds, unknown/reconnect, prompts, fragmented/oversize lines; no port opened)");
    return 0;
}
#endif
