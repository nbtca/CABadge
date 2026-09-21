"""Exercise production LCD rectangle/byte order/error handling and touch boundaries on host."""
from pathlib import Path
import os,re,subprocess
root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/physical_display.c').read_text(encoding='utf-8')
names=('lcd_window','lcd_copy','backlight_duty','touch_decode','physical_display_flush','collect_dma','dma_start','dma_finish','physical_touch_read')
functions='\n'.join(re.search(r'(?:static )?(?:void|bool|unsigned)(?: IRAM_ATTR)? '+n+r'\(.*?\n}',source,re.S).group() for n in names)
unit=root/'build/physical_display_check.c'
unit.write_text(r"""#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define LCD_ROWS 32
#define ESP_OK 0
#define portMAX_DELAY 0
#define SPI_TRANS_MODE_QIO 1
#define SPI_TRANS_VARIABLE_CMD 2
#define SPI_TRANS_VARIABLE_ADDR 4
#define SPI_TRANS_VARIABLE_DUMMY 8
#define SPI_TRANS_CS_KEEP_ACTIVE 16
#define ESP_ERROR_CHECK(e) assert((e)==0)
typedef int esp_err_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int cmd,addr,flags;size_t length;const void *tx_buffer;void *user;} spi_transaction_t;
typedef struct {spi_transaction_t base;unsigned command_bits,address_bits,dummy_bits;} spi_transaction_ext_t;
static uint8_t storage[2][360*LCD_ROWS*2],*dma[2]={storage[0],storage[1]};
static spi_transaction_ext_t transfers[2];
static int lcd,display_error;
static uint32_t flush_us,copy_us;
static bool profiling;
typedef struct {uint32_t queue_us,wire_us,wait_us;} lcd_profile_t;
static lcd_profile_t profile;
typedef struct {int64_t queued,started,finished;} dma_stamp_t;
static dma_stamp_t stamps[2];
static void collect_dma(spi_transaction_t *tx);
static void dma_start(spi_transaction_t *tx);
static void dma_finish(spi_transaction_t *tx);
#define IRAM_ATTR
static bool display_ok=true,painted,off,acquired;
static int commands,fail_command,queues,fail_queue,queued,finished,ended;
static size_t consumed;
static uint8_t captured[360*360*2];
static spi_transaction_t *pending[32];
static struct {uint8_t command,data[4];} windows[4];
static int64_t fake_time;
static int64_t esp_timer_get_time(void){return fake_time+=10;}
typedef struct {int dummy;} lv_indev_t;
typedef struct {struct {int x,y;} point;int state;} lv_indev_data_t;
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0
static bool touch_ok=true;static int touch,touch_error,resets,waits;static uint8_t touch_raw[6];
static int i2c_master_transmit_receive(int handle,const void *reg,size_t rn,void *out,size_t n,int timeout){
 (void)handle;(void)reg;(void)rn;(void)timeout;memcpy(out,touch_raw,n);return touch_error;
}
static void lv_indev_reset(lv_indev_t *i,void *obj){(void)i;(void)obj;resets++;}
static void lv_indev_wait_release(lv_indev_t *i){(void)i;waits++;}
static esp_err_t spi_device_acquire_bus(int handle,int wait){(void)handle;(void)wait;assert(!acquired);acquired=true;return 0;}
static void spi_device_release_bus(int handle){(void)handle;assert(acquired&&finished==queued);acquired=false;}
static esp_err_t lcd_tx(uint8_t command,const void *data,size_t bytes,bool color){
 (void)color;assert(acquired&&commands<2&&bytes==4);windows[commands].command=command;
 memcpy(windows[commands].data,data,bytes);return ++commands==fail_command?7:0;
}
static esp_err_t spi_device_queue_trans(int handle,spi_transaction_t *tx,int wait){
 (void)handle;(void)wait;assert(acquired&&queued-finished<2);
 if(++queues==fail_queue)return 7;
 if(!queued)assert(tx->cmd==0x32&&tx->addr==0x2c00&&!(tx->flags&SPI_TRANS_VARIABLE_CMD));
 else assert((tx->flags&14)==14&&((spi_transaction_ext_t*)tx)->command_bits==0&&((spi_transaction_ext_t*)tx)->address_bits==0);
 pending[queued++]=tx;dma_start(tx);return 0;
}
static esp_err_t spi_device_get_trans_result(int handle,spi_transaction_t **done,int wait){
 (void)handle;(void)wait;assert(acquired&&finished<queued);
 spi_transaction_t *tx=pending[finished++];size_t size=tx->length/8;
 assert(size<=360*LCD_ROWS*2&&consumed+size<=sizeof(captured));
 memcpy(captured+consumed,tx->tx_buffer,size);consumed+=size;
 if(!(tx->flags&SPI_TRANS_CS_KEEP_ACTIVE))ended++;
 dma_finish(tx);*done=tx;return 0;
}
static esp_err_t spi_device_polling_transmit(int handle,spi_transaction_t *tx){(void)handle;assert(acquired&&finished==queued&&!tx->length);ended++;return 0;}
static void physical_display_backlight(int brightness,bool asleep){off=asleep&&brightness==0;}
static void reset(void){commands=queues=queued=finished=ended=0;consumed=0;fail_command=fail_queue=0;display_error=0;display_ok=true;painted=off=false;}
"""+functions+r"""
int main(void){
 static uint8_t pixels[360*360*2],original[sizeof(pixels)];
 for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(i*31+i/97)&255;
 memcpy(original,pixels,sizeof(pixels));
 physical_display_flush(&(lv_area_t){0,0,359,359},pixels);
 assert(painted&&display_ok&&!acquired&&commands==2&&queued==12&&finished==12&&ended==1&&consumed==sizeof(pixels));
 assert(windows[0].command==0x2a&&windows[1].command==0x2b);
 assert(!memcmp(windows[0].data,(uint8_t[]){0,0,1,103},4));
 for(unsigned i=0;i<sizeof(pixels);i+=2){assert(captured[i]==pixels[i+1]);assert(captured[i+1]==pixels[i]);}
 assert(!memcmp(pixels,original,sizeof(pixels))&&flush_us>0&&copy_us>0);
 reset();profiling=true;physical_display_flush(&(lv_area_t){358,327,359,359},pixels);
 assert(commands==2&&queued==2&&consumed==132&&ended==1);assert(profile.queue_us>0&&profile.wire_us>0&&profile.wait_us>0);
 assert(!memcmp(windows[0].data,(uint8_t[]){1,102,1,103},4));
 assert(!memcmp(windows[1].data,(uint8_t[]){1,71,1,103},4));
 reset();physical_display_flush(&(lv_area_t){0,0,360,359},pixels);assert(commands==0&&queues==0);
 reset();fail_queue=3;physical_display_flush(&(lv_area_t){0,0,359,359},pixels);
 assert(!painted&&!display_ok&&display_error==7&&!display_ok&&finished==queued&&ended==1&&!acquired);
 int before=queues;physical_display_flush(&(lv_area_t){0,0,359,359},pixels);assert(queues==before);
 reset();fail_command=2;physical_display_flush(&(lv_area_t){0,0,359,359},pixels);assert(!display_ok&&!queued&&!acquired);
 assert(backlight_duty(100,false)==51&&backlight_duty(200,false)==51&&backlight_duty(70,true)==0&&backlight_duty(-1,false)==0);
 int x=12,y=13;uint8_t raw[]={0,1,1,103,1,103};assert(touch_decode(raw,&x,&y)&&x==359&&y==359);
 raw[3]=104;assert(!touch_decode(raw,&x,&y)&&x==359);raw[3]=0;raw[1]=0;assert(!touch_decode(raw,&x,&y));
 lv_indev_t indev={0};lv_indev_data_t input={0};
 memcpy(touch_raw,(uint8_t[]){0,1,0,100,0,120},6);fake_time+=20000;
 physical_touch_read(&indev,&input);assert(input.state==1&&input.point.x==100&&resets==0);
 touch_error=7;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==1&&waits==1);
 touch_error=0;touch_raw[1]=0;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==1);
 touch_raw[1]=1;touch_raw[2]=1;touch_raw[3]=104;fake_time+=20000;physical_touch_read(&indev,&input);assert(input.state==0&&resets==2&&waits==2);
 puts("Production LCD: whole frame, continuous CS, DMA lifetime/byte order, error drain/blackout, bounds PASS");
}
""",encoding='utf-8')

env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=root/'build/physical_display_check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
