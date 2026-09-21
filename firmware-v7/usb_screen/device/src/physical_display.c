#include "physical_display.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "panel_init.h"
#include <stdatomic.h>

/* c7c59dff; supplier init, 40 MHz; one complete frame per CS-held pixel stream. */
#define LCD_ROWS 32
static spi_device_handle_t lcd;
static i2c_master_dev_handle_t touch;
static uint8_t *dma[2];
static spi_transaction_ext_t transfers[2];
static uint32_t flush_us,copy_us;
static bool profiling;
static int actual_hz;
static lcd_profile_t profile;
typedef struct {int64_t queued,started,finished;} dma_stamp_t;
static dma_stamp_t stamps[2];
static void IRAM_ATTR dma_start(spi_transaction_t *tx){
    if(tx->user)((dma_stamp_t*)tx->user)->started=esp_timer_get_time();
}
static void IRAM_ATTR dma_finish(spi_transaction_t *tx){
    if(tx->user)((dma_stamp_t*)tx->user)->finished=esp_timer_get_time();
}
static void collect_dma(spi_transaction_t *tx){
    dma_stamp_t *s=tx->user;if(!s)return;
    profile.queue_us+=(uint32_t)(s->started-s->queued);
    profile.wire_us+=(uint32_t)(s->finished-s->started);
}
void physical_display_profile(bool on){profiling=on;}
lcd_profile_t physical_display_profile_result(void){return profile;}
int physical_display_clock_hz(void){return actual_hz;}
static _Atomic esp_err_t display_error;
static atomic_bool display_ok,painted;
static bool touch_ok;
static esp_err_t lcd_tx(uint8_t command,const void *data,size_t bytes,bool color){
    spi_transaction_t tx={.cmd=color?0x32:0x02,.addr=(uint32_t)command<<8,
        .length=bytes*8,.tx_buffer=bytes?data:NULL,.flags=color?SPI_TRANS_MODE_QIO:0};
    return spi_device_polling_transmit(lcd,&tx);
}
static void lcd_window(uint8_t out[4],int first,int last){
    out[0]=first>>8;out[1]=first;out[2]=last>>8;out[3]=last;
}
static void lcd_copy(uint8_t *out,const uint8_t *in,size_t bytes){
    for(size_t i=0;i<bytes;i+=2){out[i]=in[i+1];out[i+1]=in[i];}
}
static unsigned backlight_duty(int brightness,bool asleep){
    if(asleep||brightness<=0)return 0;
    if(brightness>100)brightness=100;
    /* First-board ceiling: 51/1024 (~5%); raise only after LED current measurement. */
    return (unsigned)brightness*51/100;
}
void physical_display_init(void){
    gpio_set_level(6,0);gpio_set_direction(6,GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(10));gpio_set_level(6,1);vTaskDelay(pdMS_TO_TICKS(50));
    spi_bus_config_t bus={.sclk_io_num=12,.mosi_io_num=11,.miso_io_num=13,
        .quadwp_io_num=14,.quadhd_io_num=9,.max_transfer_sz=360*LCD_ROWS*2,
        .flags=SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_QUAD};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev={.command_bits=8,.address_bits=24,.clock_speed_hz=40000000,
        .pre_cb=dma_start,.post_cb=dma_finish,.mode=0,.spics_io_num=10,.queue_size=2,.cs_ena_pretrans=1,.cs_ena_posttrans=1,.flags=SPI_DEVICE_HALFDUPLEX};
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST,&dev,&lcd));
    ESP_ERROR_CHECK(spi_device_get_actual_freq(lcd,&actual_hz));actual_hz*=1000;
    gpio_set_level(7,0);gpio_set_direction(7,GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(10));gpio_set_level(7,1);vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(lcd_tx(0x36,(uint8_t[]){0},1,false));
    for(unsigned i=0;i<sizeof(panel_init)/sizeof(panel_init[0]);i++){
        ESP_ERROR_CHECK(lcd_tx(panel_init[i].command,panel_init[i].data,panel_init[i].count,false));
        if(panel_init[i].delay_ms)vTaskDelay(pdMS_TO_TICKS(panel_init[i].delay_ms));
    }
    for(int i=0;i<2;i++){
        dma[i]=heap_caps_malloc(360*LCD_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
        ESP_ERROR_CHECK(dma[i]?ESP_OK:ESP_ERR_NO_MEM);
    }
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=1000,.clk_cfg=LEDC_AUTO_CLK};
    ledc_channel_config_t channel={.gpio_num=1,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=0};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));ESP_ERROR_CHECK(ledc_channel_config(&channel));
    display_ok=true;
}
void physical_display_flush(const lv_area_t *a,const uint8_t *pixels){
    flush_us=copy_us=0;profile=(lcd_profile_t){0};
    if(!display_ok)return;
    if(a->x1<0||a->y1<0||a->x2>=360||a->y2>=360||a->x1>a->x2||a->y1>a->y2)return;
    int64_t started=esp_timer_get_time();
    int w=a->x2-a->x1+1,h=a->y2-a->y1+1;uint8_t window[4];
    esp_err_t err=spi_device_acquire_bus(lcd,portMAX_DELAY);
    if(err!=ESP_OK){display_error=err;display_ok=false;return;}
    lcd_window(window,a->x1,a->x2);err=lcd_tx(0x2a,window,4,false);
    if(err==ESP_OK){lcd_window(window,a->y1,a->y2);err=lcd_tx(0x2b,window,4,false);}
    unsigned pending=0,slot=0;
    for(int row=0;row<h&&err==ESP_OK;row+=LCD_ROWS){
        if(pending==2){
            spi_transaction_t *done;
            int64_t waiting=profiling?esp_timer_get_time():0;
            ESP_ERROR_CHECK(spi_device_get_trans_result(lcd,&done,portMAX_DELAY));pending--;
            if(profiling){profile.wait_us+=(uint32_t)(esp_timer_get_time()-waiting);collect_dma(done);}
        }
        int rows=h-row<LCD_ROWS?h-row:LCD_ROWS;size_t bytes=w*rows*2;
        int64_t copying=esp_timer_get_time();
        lcd_copy(dma[slot],pixels+row*w*2,bytes);copy_us+=(uint32_t)(esp_timer_get_time()-copying);
        spi_transaction_ext_t *tx=&transfers[slot];*tx=(spi_transaction_ext_t){0};
        tx->base=(spi_transaction_t){.cmd=0x32,.addr=0x002c00,.length=bytes*8,
            .tx_buffer=dma[slot],.flags=SPI_TRANS_MODE_QIO};
        /* First chunk carries RAMWR. The remaining chunks continue the same CS-low data phase. */
        if(row)tx->base.flags|=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;
        if(row+rows<h)tx->base.flags|=SPI_TRANS_CS_KEEP_ACTIVE;
        if(profiling){stamps[slot]=(dma_stamp_t){.queued=esp_timer_get_time()};tx->base.user=&stamps[slot];}
        err=spi_device_queue_trans(lcd,&tx->base,portMAX_DELAY);
        if(err==ESP_OK){pending++;slot^=1;}
    }
    while(pending){
        spi_transaction_t *done;
        int64_t waiting=profiling?esp_timer_get_time():0;
            ESP_ERROR_CHECK(spi_device_get_trans_result(lcd,&done,portMAX_DELAY));pending--;
            if(profiling){profile.wait_us+=(uint32_t)(esp_timer_get_time()-waiting);collect_dma(done);}
    }
    if(err!=ESP_OK){
        /* End any held CS after draining DMA. A failed frame is never counted as presented. */
        spi_transaction_ext_t end={.base={.flags=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY}};
        spi_device_polling_transmit(lcd,&end.base);
    }
    spi_device_release_bus(lcd);
    flush_us=(uint32_t)(esp_timer_get_time()-started);display_error=err;
    if(err!=ESP_OK){display_ok=false;}else painted=true;
}
uint32_t physical_display_flush_us(void){return flush_us;}
uint32_t physical_display_copy_us(void){return copy_us;}
int physical_display_error(void){return display_error;}
void physical_display_backlight(int brightness,bool asleep){
    static int previous=-1;
    int duty=backlight_duty(brightness,asleep||!display_ok||!painted);
    if(duty==previous)return;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0));previous=duty;
}
static bool touch_decode(const uint8_t data[6],int *x,int *y){
    int nx=((data[2]&15)<<8)|data[3],ny=((data[4]&15)<<8)|data[5];
    if((data[1]&15)!=1||nx>=360||ny>=360)return false;
    *x=nx;*y=ny;return true;
}
void physical_touch_init(void){
    /* Reuse motion service I2C0: creating another bus would break shake wake. */
    i2c_master_bus_handle_t bus;
    if(i2c_master_get_bus_handle(I2C_NUM_0,&bus)!=ESP_OK)return;
    if(i2c_master_probe(bus,0x15,20)!=ESP_OK)return;
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x15,.scl_speed_hz=100000};
    if(i2c_master_bus_add_device(bus,&dev,&touch)!=ESP_OK)return;
    uint8_t reg=0xa7,id[3];
    if(i2c_master_transmit_receive(touch,&reg,1,id,3,20)!=ESP_OK)return;
    uint8_t awake[]={0xfe,1};
    touch_ok=i2c_master_transmit(touch,awake,sizeof(awake),20)==ESP_OK;
}
void physical_touch_read(lv_indev_t *indev,lv_indev_data_t *data){
    (void)indev;static int x,y;static bool down;static int64_t last;
    int64_t now=esp_timer_get_time();
    /* ponytail: bounded 50 Hz polling; use IRQ scheduling if power/latency measurements require it. */
    if(touch_ok&&now-last>=20000){
        last=now;uint8_t reg=0x01,raw[6];
        esp_err_t rc=i2c_master_transmit_receive(touch,&reg,1,raw,sizeof(raw),20);
        bool valid=rc==ESP_OK&&touch_decode(raw,&x,&y);
        if(rc!=ESP_OK||((raw[1]&15)&&!valid)){
            /* A lost sample cancels the contact; it is not a tap or fling. */
            lv_indev_reset(indev,NULL);lv_indev_wait_release(indev);
        }
        down=valid;
    }
    data->point.x=x;data->point.y=y;data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
