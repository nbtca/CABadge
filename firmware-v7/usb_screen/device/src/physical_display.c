#include "physical_display.h"
#include "src/misc/lv_timer_private.h"
#include "frame_trace.h"
#include "backlight_curve.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "panel_init.h"
#include <stdatomic.h>
#include <string.h>

static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static i2c_master_dev_handle_t touch;
static lv_display_t *display;
static lv_timer_cb_t adapter_refresh;
static bool touch_ok;
static atomic_bool painted,completed,transition_owner;
static _Atomic esp_err_t display_error;
static int64_t transfer_start;
static uint32_t transfer_us;
static uint8_t *staging[2];
static atomic_bool notify_flush;
static bool IRAM_ATTR color_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *ctx){
    if(atomic_load_explicit(&transition_owner,memory_order_acquire)){atomic_store(&painted,true);return false;}
    if(!atomic_load_explicit(&notify_flush,memory_order_acquire))return false;
    frame_trace(5,0);(void)io;(void)event;(void)ctx;
    transfer_us=(uint32_t)(esp_timer_get_time()-transfer_start);
    atomic_store(&painted,true);
    bool wake=esp_lv_adapter_display_notify_color_trans_done_from_isr(display);
    atomic_store_explicit(&completed,true,memory_order_release);
    return wake;
}
const char *physical_display_readback(void){return "{\"driver\":\"esp_lcd_st77916\",\"qspi_config_hz\":40000000,\"readback_available\":false}";}
int physical_display_clock_hz(void){return 40000000;}
int physical_display_error(void){return display_error;}
esp_lcd_panel_handle_t physical_display_panel(void){return panel;}
esp_lcd_panel_io_handle_t physical_display_io(void){return panel_io;}
static void owned_refresh(lv_timer_t *timer){
    /* Layout dirtiness can resume a refresh timer even with invalidation off.
     * Gate the actual entry too; input/service/animation timers stay untouched. */
    if(physical_display_transition_active()){lv_timer_pause(timer);return;}
    adapter_refresh(timer);
}
void physical_display_bind(lv_display_t *disp){
    display=disp;
    lv_timer_t *refresh=lv_display_get_refr_timer(display);
    adapter_refresh=refresh->timer_cb;lv_timer_set_cb(refresh,owned_refresh);
    esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=color_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(panel_io,&callbacks,NULL));
}
esp_err_t physical_display_draw(int x1,int y1,int x2,int y2,const void *pixels){
    if(atomic_load_explicit(&transition_owner,memory_order_acquire))return ESP_ERR_INVALID_STATE;
    transfer_start=esp_timer_get_time();
    esp_err_t err=ESP_OK;
    int width=x2-x1;
    if(width<=0||width>360||y2<=y1)return ESP_ERR_INVALID_ARG;
    /* Stride is the compact LVGL flush area. Drain before reusing a DMA source.
     * Internal sources avoid the SPI driver's two ~24KB PSRAM bounce buffers. */
    for(int y=y1,index=0;y<y2;y+=PHYSICAL_STAGING_ROWS,index^=1){
        int rows=y2-y;if(rows>PHYSICAL_STAGING_ROWS)rows=PHYSICAL_STAGING_ROWS;
        err=esp_lcd_panel_io_tx_param(panel_io,-1,NULL,0);if(err!=ESP_OK)break;
        memcpy(staging[index],(const uint8_t*)pixels+(y-y1)*width*2,rows*width*2);
        atomic_store_explicit(&notify_flush,y+rows==y2,memory_order_release);
        err=esp_lcd_panel_draw_bitmap(panel,x1,y,x2,y+rows,staging[index]);
        if(err!=ESP_OK)break;
    }
    frame_trace(4,err);
    if(err!=ESP_OK){
        /* Drain any successfully queued chunks before adapter releases its draw buffer. */
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io,-1,NULL,0));
        display_error=err;
    }
    return err;
}
void physical_display_wait(void){
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io,-1,NULL,0));
}
bool physical_display_complete(uint32_t *us){
    if(!atomic_exchange_explicit(&completed,false,memory_order_acq_rel))return false;
    *us=transfer_us;return true;
}
bool physical_display_transition_active(void){return atomic_load_explicit(&transition_owner,memory_order_acquire);}
void *physical_display_transition_buffer(unsigned index){return physical_display_transition_active()&&index<2?staging[index]:NULL;}
bool physical_display_transition_acquire(void){
    /* Called under the existing GUI lock, outside any refresh callback. Pausing
     * this timer leaves the adapter's input, animation and service timers alive. */
    if(!display||physical_display_transition_active()||display_error||!lv_display_is_invalidation_enabled(display))return false;
    lv_timer_t *timer=lv_display_get_refr_timer(display);
    if(!timer)return false;
    lv_timer_pause(timer);
    esp_err_t err=esp_lcd_panel_io_tx_param(panel_io,-1,NULL,0);
    if(err!=ESP_OK){lv_timer_resume(timer);return false;}
    lv_display_enable_invalidation(display,false);
    atomic_store_explicit(&transition_owner,true,memory_order_release);
    return true;
}
esp_err_t physical_display_transition_draw(int x1,int y1,int x2,int y2,const void *pixels){
    if(!physical_display_transition_active())return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_draw_bitmap(panel,x1,y1,x2,y2,pixels);
}
esp_err_t physical_display_transition_drain(void){
    return esp_lcd_panel_io_tx_param(panel_io,-1,NULL,0);
}
void physical_display_transition_release(void){
    if(!physical_display_transition_active())return;
    /* Caller has joined the worker. Drain also reclaims SPI descriptors before
     * its source memory is reusable; elapsed time is never a completion signal. */
    ESP_ERROR_CHECK(physical_display_transition_drain());
    atomic_store_explicit(&transition_owner,false,memory_order_release);
    lv_display_enable_invalidation(display,true);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_timer_resume(lv_display_get_refr_timer(display));
    lv_timer_ready(lv_display_get_refr_timer(display));
}
unsigned physical_display_backlight_duty(void){return ledc_get_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);}
void physical_display_init(void){
    gpio_set_level(6,0);gpio_set_direction(6,GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(10));gpio_set_level(6,1);vTaskDelay(pdMS_TO_TICKS(50));
    spi_bus_config_t bus=ST77916_PANEL_BUS_QSPI_CONFIG(12,11,13,14,9,360*32*2);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io=ST77916_PANEL_IO_QSPI_CONFIG(10,NULL,NULL);
    io.pclk_hz=40000000;io.trans_queue_depth=2;io.cs_ena_pretrans=1;io.cs_ena_posttrans=1;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&panel_io));
    for(unsigned i=0;i<2;i++){
        staging[i]=heap_caps_malloc(360*PHYSICAL_STAGING_ROWS*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
        ESP_ERROR_CHECK(staging[i]?ESP_OK:ESP_ERR_NO_MEM);
    }
    /* Preserve this module's verified supplier sequence, not the driver's generic panel defaults. */
    size_t count=sizeof(panel_init)/sizeof(panel_init[0]);
    st77916_lcd_init_cmd_t *commands=heap_caps_calloc(count+1,sizeof(*commands),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(commands?ESP_OK:ESP_ERR_NO_MEM);
    static const uint8_t madctl=0;
    commands[0]=(st77916_lcd_init_cmd_t){0x36,&madctl,1,0};
    for(size_t i=0;i<count;i++)commands[i+1]=(st77916_lcd_init_cmd_t){panel_init[i].command,panel_init[i].data,panel_init[i].count,panel_init[i].delay_ms};
    st77916_vendor_config_t vendor={.init_cmds=commands,.init_cmds_size=count+1,.flags.use_qspi_interface=1};
    esp_lcd_panel_dev_config_t cfg={.reset_gpio_num=7,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,.vendor_config=&vendor};
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io,&cfg,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=1000,.clk_cfg=LEDC_AUTO_CLK};
    ledc_channel_config_t channel={.gpio_num=1,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=0};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));ESP_ERROR_CHECK(ledc_channel_config(&channel));
}
void physical_display_backlight(int brightness,bool asleep){
    static int previous=-1;
    int duty=backlight_duty(brightness,asleep||display_error||!painted);
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
