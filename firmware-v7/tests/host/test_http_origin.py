"""Compile the production HTTP Host/Origin gate against mocked httpd and Wi-Fi state."""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
import re
import subprocess

source = (ROOT/'usb_screen/device/src/wallpaper_service.c').read_text(encoding='utf-8')
functions = ''.join(re.search(r'^static bool '+name+r'\(.*?^}\n', source, re.S | re.M).group() for name in ('own_host', 'web_request'))
out = TEST_TEMP/'http-origin-test'
out.mkdir(parents=True, exist_ok=True)
(out/'check.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
typedef int esp_err_t;
typedef struct {const char *host,*origin;} httpd_req_t;
static char station_ip[24];static bool hotspot;
static void wifi_wallpaper_address(char *ip,size_t n){snprintf(ip,n,"%s",station_ip);}
static bool wifi_wallpaper_hotspot_active(void){return hotspot;}
static bool management_begin_request(uint32_t *generation){*generation=7;return true;}
static const char *header(httpd_req_t *r,const char *name){return !strcmp(name,"Host")?r->host:!strcmp(name,"Origin")?r->origin:NULL;}
static size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name){const char *v=header(r,name);return v?strlen(v):0;}
static esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t n){
 const char *v=header(r,name);if(!v||strlen(v)>=n)return -1;strcpy(out,v);return ESP_OK;
}
''' + functions + r'''
static bool allowed(const char *host,const char *origin){httpd_req_t r={host,origin};uint32_t g=0;return web_request(&r,&g)&&g==7;}
int main(void){
 strcpy(station_ip,"10.0.0.23");
 assert(allowed("10.0.0.23",NULL)&&allowed("10.0.0.23:80",NULL)&&allowed("10.0.0.23","http://10.0.0.23"));
 assert(!allowed("evil.example","http://evil.example")&&!allowed("evil.example",NULL));
 assert(!allowed(NULL,NULL)&&!allowed("10.0.0.2",NULL)&&!allowed("10.0.0.233",NULL)&&!allowed("10.0.0.23:8080",NULL));
 assert(!allowed("10.0.0.23","http://evil.example")&&!allowed("10.0.0.23.evil.example",NULL));
 assert(!allowed("192.168.4.1",NULL));
 hotspot=true;assert(allowed("192.168.4.1","http://192.168.4.1")&&!allowed("192.168.4.10",NULL));
 station_ip[0]=0;assert(!allowed("",NULL)&&allowed("192.168.4.1",NULL));
 puts("PASS HTTP Host gate: station and hotspot addresses only, port 80 only, rebinding names and mismatched Origin rejected");
}
''', encoding='utf-8')
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP), ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe = out/'check.exe'
subprocess.run([str(ZIG), 'cc', '-O2', '-UNDEBUG', '-Wall', '-Werror', str(out/'check.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
