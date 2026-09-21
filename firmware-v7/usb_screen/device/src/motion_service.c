#include "ui/badge_ui.h"
#include "shake_detector.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"

static i2c_master_dev_handle_t accel;
static bool ready;
static shake_detector_t detector;
static esp_err_t read_reg(uint8_t reg,void *data,size_t size){return i2c_master_transmit_receive(accel,&reg,1,data,size,20);}
static esp_err_t write_reg(uint8_t reg,uint8_t value){uint8_t data[]={reg,value};return i2c_master_transmit(accel,data,2,20);}
void motion_service_init(void){
    /* Fabricated c7c59dff: SCL GPIO2, SDA GPIO3, SC7A20 SA0 tied to GND. */
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t cfg={.i2c_port=I2C_NUM_0,.sda_io_num=3,.scl_io_num=2,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=false};
    if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK)return;
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x18,.scl_speed_hz=100000};
    if(i2c_master_bus_add_device(bus,&dev,&accel)!=ESP_OK)return;
    uint8_t id=0,ctrl1=0,ctrl4=0;
    if(read_reg(0x0f,&id,1)!=ESP_OK||id!=0x11)return;
    if(write_reg(0x23,0x88)!=ESP_OK||write_reg(0x20,0x47)!=ESP_OK)return;
    ready=read_reg(0x20,&ctrl1,1)==ESP_OK&&read_reg(0x23,&ctrl4,1)==ESP_OK&&ctrl1==0x47&&ctrl4==0x88;
}
void motion_service_poll(void){
    if(!ready)return;
    if(!badge_ui_is_asleep()){detector=(shake_detector_t){0};return;}
    static uint32_t previous;
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    if(now-previous<20)return;
    previous=now;
    uint8_t status,bytes[6];
    if(read_reg(0x27,&status,1)!=ESP_OK){detector=(shake_detector_t){0};return;}
    if(!(status&8))return; /* Wait for a new XYZ sample. */
    if(read_reg(0x28|0x80,bytes,sizeof(bytes))!=ESP_OK){detector=(shake_detector_t){0};return;}
    int16_t xyz[3];for(int i=0;i<3;i++)xyz[i]=(int16_t)(bytes[i*2]|((uint16_t)bytes[i*2+1]<<8))/16;
    if(shake_detect(&detector,true,now,xyz))badge_ui_motion();
}
