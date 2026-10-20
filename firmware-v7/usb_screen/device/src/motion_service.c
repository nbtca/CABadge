#include "ui/badge_ui.h"
#include "ui/apps.h"
#include "ui/fluidbox.h"
#include "ui/timetiles.h"
#include "ui/motion.h"
#include "motion_service.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include <stdio.h>

static i2c_master_dev_handle_t accel;
static bool ready;
static badge_motion_t detector;
static bool automatic_sleep,pending_face_sleep;
static bool click_ready,retrying;
static uint32_t retry_at;
static uint8_t click_source,last_click_source;
static uint32_t samples,errors,shakes,taps,face_sleeps,face_wakes,last_sample;
static int16_t raw[3];
/* c7c59dff real-board directions checked 2026-10-01 with USB at the top:
 * right edge low -> +X; bottom edge low -> -raw Y; screen face-up -> +Z.
 * This confirms orientation, not a six-face gain/offset calibration. */
#define SCREEN_X_AXIS 0
#define SCREEN_X_SIGN 1
#define SCREEN_Y_AXIS 1
#define SCREEN_Y_SIGN -1
#define SCREEN_Z_SIGN 1
static esp_err_t read_reg(uint8_t reg,void *data,size_t size){return i2c_master_transmit_receive(accel,&reg,1,data,size,20);}
static esp_err_t write_reg(uint8_t reg,uint8_t value){uint8_t data[]={reg,value};return i2c_master_transmit(accel,data,2,20);}
void motion_service_init(void){
    badge_apps_motion(0,0,false,false);
    fluidbox_motion(NULL,false);
    timetiles_motion(NULL,false);
    /* Fabricated c7c59dff: SCL GPIO2, SDA GPIO3, SC7A20 SA0 tied to GND. */
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t cfg={.i2c_port=I2C_NUM_0,.sda_io_num=3,.scl_io_num=2,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=false};
    if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK)return;
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x18,.scl_speed_hz=100000};
    if(i2c_master_bus_add_device(bus,&dev,&accel)!=ESP_OK)return;
    uint8_t id=0,ctrl1=0,ctrl4=0;
    if(read_reg(0x0f,&id,1)!=ESP_OK||id!=0x11)return;
    if(write_reg(0x23,0x88)!=ESP_OK||write_reg(0x20,MOTION_CTRL1)!=ESP_OK)return;
    ready=read_reg(0x20,&ctrl1,1)==ESP_OK&&read_reg(0x23,&ctrl4,1)==ESP_OK&&ctrl1==MOTION_CTRL1&&ctrl4==0x88;
    const uint8_t config[][2]={{0x21,MOTION_CLICK_CTRL2},{0x38,MOTION_CLICK_CFG},
        {0x3a,MOTION_CLICK_THS},{0x3b,MOTION_CLICK_LIMIT},{0x3c,MOTION_CLICK_LATENCY},
        {0x3d,MOTION_CLICK_WINDOW},{0x22,0x80}};
    click_ready=ready;
    for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);i++){
        uint8_t value=0;
        if(write_reg(config[i][0],config[i][1])!=ESP_OK||read_reg(config[i][0],&value,1)!=ESP_OK||value!=config[i][1])click_ready=false;
    }
    read_reg(0x26,&ctrl1,1); /* Initialize HPF reference; does not remove gravity from XYZ. */
}
void motion_service_poll(void){
    if(!ready)return;
    static uint32_t previous;
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    if(retrying){if(now-retry_at<1000)return;retrying=false;}
    if(now-previous<10)return;
    previous=now;
    uint8_t bytes[7];
    /* Status plus BDU-protected XYZ in one transaction; never reuse old data. */
    if(read_reg(0x27|0x80,bytes,sizeof(bytes))!=ESP_OK){
        errors++;detector=(badge_motion_t){0};badge_apps_motion(0,0,false,false);
        fluidbox_motion(NULL,false);
        timetiles_motion(NULL,false);
        retrying=true;retry_at=now;
        return;
    }
    if(!(bytes[0]&8)){if(now-last_sample>250){badge_apps_motion(0,0,false,false);fluidbox_motion(NULL,false);timetiles_motion(NULL,false);}return;}
    click_source=0;
    if(click_ready&&read_reg(0x39,&click_source,1)!=ESP_OK){errors++;click_source=0;}
    if(click_source&0x40)last_click_source=click_source;
    for(int i=0;i<3;i++)raw[i]=(int16_t)(bytes[1+i*2]|((uint16_t)bytes[2+i*2]<<8))/16;
    int16_t xyz[3]={raw[SCREEN_X_AXIS]*SCREEN_X_SIGN,raw[SCREEN_Y_AXIS]*SCREEN_Y_SIGN,raw[2]*SCREEN_Z_SIGN};
    samples++;last_sample=now;
    fluidbox_motion(xyz,true);
    timetiles_motion(xyz,true);
    bool asleep=badge_ui_is_asleep();
    if(!asleep)automatic_sleep=false;
    unsigned events=badge_motion_sample(&detector,asleep,now,xyz,click_source);
    if(events&MOTION_FACE_DOWN)pending_face_sleep=!asleep;
    if(!detector.down||xyz[2]>=-800||abs(xyz[0])>=250||abs(xyz[1])>=250)pending_face_sleep=false;
    if(events&MOTION_SHAKE){shakes++;if(!asleep)timetiles_shake();}
    if(events&MOTION_DOUBLE_TAP)taps++;
    badge_apps_motion(detector.gravity[0]/1000.f,detector.gravity[1]/1000.f,(events&MOTION_SHAKE)!=0,true);
    if(!asleep&&pending_face_sleep&&!badge_ui_transition_active()){
        pending_face_sleep=false;automatic_sleep=true;face_sleeps++;badge_ui_sleep(true);
    }else if(asleep&&automatic_sleep&&(events&MOTION_FACE_UP)){
        automatic_sleep=false;face_wakes++;badge_ui_motion();
    }else if(asleep&&((events&MOTION_DOUBLE_TAP)||(xyz[2]>-500&&(events&MOTION_SHAKE)))){
        automatic_sleep=false;badge_ui_motion();
    }
}
void motion_service_info(char *out,size_t size){
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    snprintf(out,size,"{\"ready\":%s,\"click_ready\":%s,\"click_source\":%u,\"last_click_source\":%u,\"fresh\":%s,\"raw_mg\":[%d,%d,%d],\"screen_mg\":[%d,%d,%d],\"samples\":%lu,\"errors\":%lu,\"shakes\":%lu,\"double_taps\":%lu,\"face_sleeps\":%lu,\"face_wakes\":%lu,\"automatic_sleep\":%s}",
        ready?"true":"false",click_ready?"true":"false",click_source,last_click_source,detector.valid&&samples&&now-last_sample<250?"true":"false",raw[0],raw[1],raw[2],
        (int)detector.gravity[0],(int)detector.gravity[1],(int)detector.gravity[2],
        (unsigned long)samples,(unsigned long)errors,(unsigned long)shakes,(unsigned long)taps,
        (unsigned long)face_sleeps,(unsigned long)face_wakes,automatic_sleep?"true":"false");
}
