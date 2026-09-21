#include "ui/badge_ui.h"
#include "physical_display.h"
#include "display_worker.h"
#include "protocol.h"
#include "bridge_protocol.h"
#include "perf_stats.h"
#include "runtime_monitor.h"
#include "wallpaper_service.h"
#include "management.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t gui_wake;
static esp_timer_handle_t gui_timer;
static void gui_wake_up(void *unused){(void)unused;xSemaphoreGive(gui_wake);}
static void gui_wait(int64_t started){
    /* Limit idle polling to 5 ms; a long render already consumes that budget. */
    int64_t remaining=5000-(esp_timer_get_time()-started);
    if(remaining<=0)return;
    if(gui_timer&&esp_timer_start_once(gui_timer,remaining)==ESP_OK)xSemaphoreTake(gui_wake,portMAX_DELAY);
    else vTaskDelay(1);
}

static badge_state_t state={.brightness=70,.wallpaper_index=-1,.battery_mv=-1,.wifi_rssi=-127,.live=true,.wifi_enabled=true};
void wifi_service_init(badge_state_t *state);
void wifi_service_poll(void);
void ble_service_init(badge_state_t *state);
void ble_service_poll(void);
void motion_service_init(void);
void motion_service_poll(void);
static jx_parser_t *parser;
static uint8_t *packet,rectangle[4105];
static char *usb_snapshot;
static bool attached;
static bool bridge;
static int monitor_scene=-1;
static uint32_t monitor_epoch;
static uint32_t tick(void);
static bool probing,probe_reduced,probe_sleep,probe_lcd;
static bool flush_pending,flush_content;
static lv_display_t *lcd_display;
static void flush_wait(lv_display_t *display);
static void flush_poll(void);
static unsigned probe_count=3;
static bool probe_manual;
static struct {perf_stats_t dma,wait,touch;uint32_t loop_max;uint32_t queue_us,wire_us,wait_us,submitted,scanning_ms;int scan_result;uint32_t last_loop;int64_t touch_at;} *extra;
static int probe_scene,probe_page;
static uint32_t probe_epoch,probe_frame_bytes,probe_internal,probe_psram;
static int64_t probe_start_us;
static perf_stats_t probe_stats,probe_flush_stats,probe_frame_stats;
static uint32_t probe_flush_time,probe_copy_time,probe_wait_time,probe_max_frame_gap,probe_last_frame;
static void probe_stop(void){
    if(!probing)return;
    flush_wait(lcd_display);
    probing=false;physical_display_profile(false);free(extra);extra=NULL;lv_obj_invalidate(lv_screen_active());state.reduced_motion=probe_reduced;
    badge_ui_probe_restore(probe_page);badge_ui_sleep(probe_sleep);badge_ui_refresh();
}
static void probe_event(lv_event_t *e){
    /* Detailed legacy probe fences the frame; normal UI/runtime sampling does not. */
    if(probing&&lv_event_get_code(e)==LV_EVENT_RENDER_READY)flush_wait(lv_event_get_target(e));
    runtime_event(e);
    if(!probing||tick()-probe_epoch<500)return;
    if(lv_event_get_code(e)==LV_EVENT_RENDER_START){probe_start_us=esp_timer_get_time();probe_frame_bytes=0;probe_flush_time=0;probe_copy_time=0;probe_wait_time=0;if(extra){extra->queue_us=extra->wire_us=extra->wait_us=0;}}
    else if(lv_event_get_code(e)==LV_EVENT_RENDER_READY&&probe_start_us){
        uint32_t frame_us=(uint32_t)(esp_timer_get_time()-probe_start_us);
        perf_add(&probe_stats,frame_us>probe_wait_time?frame_us-probe_wait_time:0,probe_frame_bytes);
        perf_add(&probe_flush_stats,probe_flush_time,probe_copy_time);
        perf_add(&probe_frame_stats,frame_us,0);
        if(extra){
            perf_add(&extra->dma,extra->queue_us,extra->wire_us);perf_add(&extra->wait,extra->wait_us,0);
            if(extra->touch_at){perf_add(&extra->touch,(uint32_t)(esp_timer_get_time()-extra->touch_at),0);extra->touch_at=0;}
        }
        probe_start_us=0;
        uint32_t now=tick();if(probe_last_frame&&now-probe_last_frame>probe_max_frame_gap)probe_max_frame_gap=now-probe_last_frame;probe_last_frame=now;
        uint32_t n=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(n<probe_internal)probe_internal=n;
        n=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(n<probe_psram)probe_psram=n;
    }
}

static uint32_t bridge_seen,bridge_request,bridge_request_crc;
static uint8_t bridge_result;
static esp_err_t settings_result;
static void save_settings(void);
static nvs_handle_t settings;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
static adc_channel_t adc_channel;
static uint32_t tick(void){return (uint32_t)(esp_timer_get_time()/1000);}

static bool send_packet(uint8_t type,const void *data,size_t n){
    size_t count=jx_packet(packet,type,data,n);
    int written=usb_serial_jtag_write_bytes(packet,count,pdMS_TO_TICKS(150));
    if(written!=(int)count){attached=false;return false;}
    return true;
}
static void probe_begin_scene(void){
    probe_epoch=tick();probe_start_us=0;memset(&probe_stats,0,sizeof(probe_stats));
    memset(&probe_flush_stats,0,sizeof(probe_flush_stats));memset(&probe_frame_stats,0,sizeof(probe_frame_stats));probe_max_frame_gap=probe_last_frame=0;
    probe_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    probe_psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(extra){memset(extra,0,sizeof(*extra));extra->last_loop=tick();extra->scan_result=-1;}
    badge_ui_page(0,false);if(!probe_manual)badge_ui_probe_scene(probe_scene,0);
    if(probe_scene==5){uint8_t cmd[]={MG_VERSION,MG_WIFI_SCAN};extra->scan_result=wifi_service_control(cmd,sizeof(cmd));}
}
static void probe_poll(void){
    if(!probing)return;
    if(probe_lcd&&physical_display_error()){
        const char *error="{\"schema\":1,\"error\":\"lcd_transfer_failed\"}";send_packet(BR_PERF_RESULT,error,strlen(error));probe_stop();return;
    }
    if(!attached||!bridge||wallpaper_busy()){
        const char *error="{\"schema\":1,\"error\":\"interrupted\"}";
        if(attached)send_packet(BR_PERF_RESULT,error,strlen(error));
        probe_stop();return;
    }
    uint32_t elapsed=tick()-probe_epoch;
    if(extra){
        bool scanning,connecting;int error,count;badge_wifi_ap_t aps[12];
        wifi_service_snapshot(&scanning,&connecting,&error,aps,&count);
        if(scanning)extra->scanning_ms+=tick()-extra->last_loop;
        extra->last_loop=tick();
    }
    if(elapsed>=(probe_manual?8500:4500)){
        char result[4600];badge_ble_info_t radio={0};ble_service_snapshot(&radio);
        snprintf(result,sizeof(result),"{\"schema\":1,\"timing_mode\":\"async_frame_fence\",\"scene\":%d,\"firmware\":\"" UI_VERSION "\",\"pcb\":\"c7c59dff\",\"elapsed_ms\":%lu,\"frames\":%u,\"samples\":%u,\"render_us_p50\":%lu,\"render_us_p95\":%lu,\"render_us_total\":%llu,\"flush_bytes_p95\":%lu,\"flush_bytes_total\":%llu,\"internal_free_min\":%lu,\"psram_free_min\":%lu,\"wifi_enabled\":%s,\"wifi_connected\":%s,\"ble_enabled\":%s,\"ble_inbound\":%s,\"ble_outbound\":%s,\"lcd_connected\":true,\"lcd_transfer\":%s,\"flush_us_p95\":%lu,\"copy_us_p95\":%lu,\"frame_us_p95\":%lu,\"frame_gap_ms_max\":%lu,\"lcd_qspi_hz\":%d,\"cpu_mhz\":240,\"done\":%s}",
            probe_scene,(unsigned long)(elapsed-500),probe_stats.total,probe_stats.count,
            (unsigned long)perf_percentile(&probe_stats,50,0),(unsigned long)perf_percentile(&probe_stats,95,0),(unsigned long long)probe_stats.us,
            (unsigned long)perf_percentile(&probe_stats,95,1),(unsigned long long)probe_stats.bytes,(unsigned long)probe_internal,(unsigned long)probe_psram,
            state.wifi_enabled?"true":"false",state.wifi_connected==1?"true":"false",radio.enabled?"true":"false",radio.inbound?"true":"false",radio.outbound?"true":"false",probe_lcd?"true":"false",
            (unsigned long)perf_percentile(&probe_flush_stats,95,0),(unsigned long)perf_percentile(&probe_flush_stats,95,1),
            (unsigned long)perf_percentile(&probe_frame_stats,95,0),(unsigned long)probe_max_frame_gap,physical_display_clock_hz(),probe_scene==(int)probe_count-1?"true":"false");
        if(extra){
            size_t used=strlen(result)-1;
            snprintf(result+used,sizeof(result)-used,",\"audit\":true,\"scene_count\":%u,\"manual\":%s,\"queue_us_p95\":%lu,\"wire_us_p95\":%lu,\"dma_wait_us_p95\":%lu,\"touch_samples\":%u,\"touch_to_flush_us_p95\":%lu,\"gui_loop_us_max\":%lu,\"scanning_ms\":%lu,\"scan_result\":%d,\"lcd_updates\":%lu,\"internal_largest\":%u,\"psram_largest\":%u}",
                probe_count,probe_manual?"true":"false",(unsigned long)perf_percentile(&extra->dma,95,0),(unsigned long)perf_percentile(&extra->dma,95,1),
                (unsigned long)perf_percentile(&extra->wait,95,0),extra->touch.total,(unsigned long)perf_percentile(&extra->touch,95,0),
                (unsigned long)extra->loop_max,(unsigned long)extra->scanning_ms,extra->scan_result,(unsigned long)extra->submitted,
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        }
        if(extra){
            size_t used=strlen(result)-1;used+=snprintf(result+used,sizeof(result)-used,",\"frame_bytes\":[");
            for(unsigned i=0;i<probe_stats.count;i++)used+=snprintf(result+used,sizeof(result)-used,"%s%lu",i?",":"",(unsigned long)probe_stats.values[i].bytes);
            snprintf(result+used,sizeof(result)-used,"]}");
        }
        send_packet(BR_PERF_RESULT,result,strlen(result));
        if(++probe_scene==(int)probe_count){probe_stop();return;}probe_begin_scene();
    }
    if(!probe_manual)badge_ui_probe_scene(probe_scene,tick()-probe_epoch);
}
/* Only the GUI consumes completions and calls LVGL. The worker never accesses UI objects. */
static void flush_complete(lv_display_t *display,display_result_t v,uint32_t blocked){
    flush_pending=false;
    runtime_flush(v.flush_us,blocked,v.last,!v.error,flush_content);
    if(probing&&probe_start_us){
        probe_flush_time+=v.flush_us;probe_copy_time+=v.copy_us;probe_wait_time+=blocked;
        if(extra){extra->queue_us+=v.dma.queue_us;extra->wire_us+=v.dma.wire_us;extra->wait_us+=v.dma.wait_us;
            if(!v.error&&v.last)extra->submitted++;}
    }
    lv_display_flush_ready(display);
}
static void flush_wait(lv_display_t *display){
    if(!flush_pending)return;
    int64_t at=esp_timer_get_time();display_result_t result=display_worker_wait();
    flush_complete(display,result,(uint32_t)(esp_timer_get_time()-at));
}
static void flush_poll(void){
    display_result_t result;
    if(flush_pending&&display_worker_poll(&result))flush_complete(lcd_display,result,0);
}
static void flush(lv_display_t *display,const lv_area_t *a,uint8_t *pixels){
    if(probing&&probe_start_us)probe_frame_bytes+=lv_area_get_width(a)*lv_area_get_height(a)*2;
    if(!probing||probe_lcd){
        configASSERT(!flush_pending);flush_pending=true;flush_content=runtime_content_frame();
        display_worker_submit(a,pixels,lv_display_flush_is_last(display));return;
    }
    lv_display_flush_ready(display);
}
static void physical_pointer_read(lv_indev_t *indev,lv_indev_data_t *data){
    if(probing&&!probe_manual){data->state=LV_INDEV_STATE_RELEASED;return;}
    physical_touch_read(indev,data);
    static int x,y;static bool down;
    bool changed=data->point.x!=x||data->point.y!=y||down!=(data->state==LV_INDEV_STATE_PRESSED);
    if(extra&&probe_manual&&changed&&!extra->touch_at)extra->touch_at=esp_timer_get_time();
    x=data->point.x;y=data->point.y;down=data->state==LV_INDEV_STATE_PRESSED;
}
static void receive(uint8_t type,const uint8_t *data,size_t n,void *ctx){
    (void)ctx;
    if(type==BR_HELLO&&n==1&&data[0]==BR_VERSION){
        uint8_t ready[12]={BR_VERSION,16};jx_put16(ready+2,360);jx_put16(ready+4,360);
        jx_put16(ready+6,BR_CAP_SERVICES|BR_CAP_WALL_READ|BR_CAP_PERF|BR_CAP_LCD_PERF|BR_CAP_AUDIT);jx_put32(ready+8,BR_PCB);
        probe_stop();bridge=true;bridge_seen=tick();bridge_request=0;
        attached=send_packet(BR_READY,ready,sizeof(ready));return;
    }
    if(bridge&&attached){
        bridge_seen=tick();
        if(type==BR_PERF&&n==1&&data[0]<=4){
            if(!data[0]){probe_stop();const char *reply="{\"schema\":1,\"cancelled\":true}";send_packet(BR_PERF_RESULT,reply,strlen(reply));return;}
            if(probing||wallpaper_busy()){const char *reply="{\"schema\":1,\"error\":\"busy\"}";send_packet(BR_PERF_RESULT,reply,strlen(reply));return;}
            flush_wait(lcd_display);
            probe_reduced=state.reduced_motion;probe_page=badge_ui_current_page();probe_sleep=badge_ui_is_asleep();
            probe_count=data[0]==3?6:3;probe_manual=data[0]==4;
            if(probe_manual)probe_count=1;
            if(data[0]>=3){extra=heap_caps_calloc(1,sizeof(*extra),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!extra)return;}
            physical_display_profile(extra!=NULL);
            probe_lcd=data[0]>=2;state.reduced_motion=false;badge_ui_sleep(false);probing=true;probe_scene=0;probe_begin_scene();return;
        }
        if(type==41){
            /* Local diagnostic controls: no credentials or persistent setting changes. */
            if(n==2&&data[0]==1&&data[1]<=8){monitor_scene=-1;badge_ui_probe_restore(data[1]);}
            else if(n==2&&data[0]==2&&data[1]<=1)badge_ui_perf_enable(data[1]);
            else if(n==2&&data[0]==3&&data[1]<=2){monitor_scene=data[1];monitor_epoch=tick();badge_ui_sleep(false);}
            else if(n!=0)return;
            if(runtime_snapshot(usb_snapshot,8192))send_packet(42,usb_snapshot,strlen(usb_snapshot));
            return;
        }
        if(type==BR_STATE&&n==0){if(management_json(usb_snapshot,8192))send_packet(BR_STATE,usb_snapshot,strlen(usb_snapshot));return;}
        if(type==BR_COMMAND){
            if(!br_command_valid(data,n)){if(n>=4){uint8_t ack[5];memcpy(ack,data,4);ack[4]=MG_INVALID;send_packet(BR_ACK,ack,5);}return;}
            uint32_t id=jx_u32(data),crc=~jx_crc(~0u,data,n);int result=MG_INVALID;
            if(id==bridge_request)result=crc==bridge_request_crc?bridge_result:MG_INVALID;
            else {
                const uint8_t *cmd=data+4;size_t size=n-4;
                result=(probing||wallpaper_busy())?MG_BUSY:MG_OK;
                if(!result){
                    int previous_index=state.wallpaper_index,previous_brightness=state.brightness;bool previous_reduced=state.reduced_motion;
                    settings_result=ESP_OK;
                    if(cmd[1]<=MG_WIFI_DISCONNECT)result=wifi_service_control(cmd,size);
                    else if(cmd[1]<=MG_BLE_DISCONNECT)result=ble_service_control(cmd,size);
                    else if(cmd[1]==MG_BRIGHTNESS)badge_ui_settings(cmd[2],state.reduced_motion);
                    else if(cmd[1]==MG_REDUCED)badge_ui_settings(state.brightness,cmd[2]);
                    else if(cmd[1]==MG_SLEEP)badge_ui_sleep(cmd[2]);
                    else if(cmd[1]==BR_APPLY)result=badge_ui_select_wallpaper(cmd[2])?MG_OK:MG_INVALID;
                    else if(cmd[1]==BR_MANAGEMENT)management_toggle();
                    else if(cmd[1]==BR_HOTSPOT)badge_wallpaper_hotspot();
                    else if(cmd[1]==BR_SHAKE)badge_ui_motion();
                    if(settings_result!=ESP_OK){state.wallpaper_index=previous_index;state.brightness=previous_brightness;state.reduced_motion=previous_reduced;badge_ui_refresh();result=MG_FAILED;}
                }
                bridge_request=id;bridge_request_crc=crc;bridge_result=result;
            }
            uint8_t ack[5];jx_put32(ack,id);ack[4]=result;send_packet(BR_ACK,ack,5);return;
        }
        if(type==BR_WALL_READ&&n==10){
            uint32_t generation=jx_u32(data),offset=jx_u32(data+4);size_t bytes=jx_u16(data+8);
            memcpy(rectangle,data,8);rectangle[8]=bytes&&bytes<=4096&&wallpaper_read_resource(generation,offset,rectangle+9,bytes)?0:1;
            send_packet(BR_WALL_DATA,rectangle,rectangle[8]?9:9+bytes);return;
        }
        /* No remote coordinate/text injection in service mode. */
        if(type==JX_TOUCH||type==JX_TEXT||type==JX_KEY||type==JX_COMMAND)return;
    }
    if(attached&&bridge&&type==JX_WALL_INFO&&n==0){
        char info[400];wallpaper_info(info,sizeof(info));send_packet(JX_WALL_INFO,info,strlen(info));
    }else if(attached&&type==JX_MANAGEMENT&&n==0){
        management_toggle();char info[400];wallpaper_info(info,sizeof(info));send_packet(JX_WALL_INFO,info,strlen(info));
    }else if(attached&&type==JX_WALL_HOTSPOT&&n==0){
        badge_wallpaper_hotspot();char info[400];wallpaper_info(info,sizeof(info));send_packet(JX_WALL_INFO,info,strlen(info));
    }else if(attached&&type>=JX_WALL_BEGIN&&type<=JX_WALL_CANCEL){
        uint32_t session=n>=4?jx_u32(data):0,offset=0;int result=2;
        if(type==JX_WALL_BEGIN&&n==8){session=0;result=wallpaper_begin(1,jx_u32(data),jx_u32(data+4),&session);}
        if(type==JX_WALL_CHUNK&&n>8){offset=jx_u32(data+4);result=wallpaper_chunk(1,session,offset,data+8,n-8);if(!result)offset+=n-8;}
        if(type==JX_WALL_FINISH&&n==4){result=wallpaper_finish_async(session);if(!result)return;}
        if(type==JX_WALL_CANCEL&&n==4){wallpaper_cancel(1,session);result=0;}
        uint8_t ack[10]={type,(uint8_t)result};jx_put32(ack+2,session);jx_put32(ack+6,offset);send_packet(JX_WALL_ACK,ack,sizeof(ack));
    }
}
static void save_settings(void){
    settings_result=ESP_ERR_INVALID_STATE;if(!settings)return;
    settings_result=nvs_set_i32(settings,"brightness",state.brightness);if(settings_result!=ESP_OK)return;
    settings_result=nvs_set_i32(settings,"reduced",state.reduced_motion);if(settings_result!=ESP_OK)return;
    settings_result=nvs_set_i32(settings,"wallpaper",state.wallpaper_index);if(settings_result==ESP_OK)settings_result=nvs_commit(settings);
}
/* Physical touch uses the same storage result as the USB service UI. */
static void local_control(int op,int value){
    int previous=state.wallpaper_index;settings_result=ESP_OK;
    if(op==BADGE_APPLY){
        bool ok=badge_ui_select_wallpaper(value)&&settings_result==ESP_OK;
        if(!ok){state.wallpaper_index=previous;badge_ui_refresh();badge_ui_notice("操作失败，请重试");}
        badge_ui_apply_result(ok);
    }else if(op==BADGE_SLEEP)badge_ui_sleep(value);
    else if(op==BADGE_BRIGHTNESS)badge_ui_settings(value,state.reduced_motion);
    else if(op==BADGE_REDUCED)badge_ui_settings(state.brightness,value);
}
static void battery_init(void){
    adc_unit_t unit;
    if(adc_oneshot_io_to_channel(8,&unit,&adc_channel)!=ESP_OK)return;
    adc_oneshot_unit_init_cfg_t cfg={.unit_id=unit};
    if(adc_oneshot_new_unit(&cfg,&adc)!=ESP_OK){adc=NULL;return;}
    adc_oneshot_chan_cfg_t channel={.atten=ADC_ATTEN_DB_6,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if(adc_oneshot_config_channel(adc,adc_channel,&channel)!=ESP_OK){adc_oneshot_del_unit(adc);adc=NULL;return;}
    adc_cali_curve_fitting_config_t cal={.unit_id=unit,.chan=adc_channel,.atten=ADC_ATTEN_DB_6,.bitwidth=ADC_BITWIDTH_DEFAULT};
    adc_cali_create_scheme_curve_fitting(&cal,&calibration);
}
static void battery_update(void){
    if(!adc||!calibration)return;
    int sum=0,raw,mv;
    for(int i=0;i<32;i++){if(adc_oneshot_read(adc,adc_channel,&raw)!=ESP_OK){state.battery_mv=-1;badge_ui_refresh();return;}sum+=raw;}
    if(adc_cali_raw_to_voltage(calibration,(sum+16)/32,&mv)==ESP_OK){
        /* Frozen R36/R37 divider is 3:1; preserve user-confirmed gain of 1.0. */
        if(state.battery_mv!=mv*3){state.battery_mv=mv*3;badge_ui_refresh();}
    }else {state.battery_mv=-1;badge_ui_refresh();}
}
void app_main(void){
    gpio_set_level(GPIO_NUM_1,0);gpio_set_direction(GPIO_NUM_1,GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_38,0);gpio_set_direction(GPIO_NUM_38,GPIO_MODE_OUTPUT);
    if(!esp_psram_is_initialized())return;
    gui_wake=xSemaphoreCreateBinary();
    esp_timer_create_args_t wake_args={.callback=gui_wake_up,.name="gui_wait"};
    if(gui_wake&&esp_timer_create(&wake_args,&gui_timer)!=ESP_OK){vSemaphoreDelete(gui_wake);gui_wake=NULL;}
    if(nvs_flash_init()==ESP_OK&&nvs_open("badge_ui",NVS_READWRITE,&settings)==ESP_OK){
        int32_t value;
        if(nvs_get_i32(settings,"brightness",&value)==ESP_OK&&value>=10&&value<=100)state.brightness=value;
        if(nvs_get_i32(settings,"reduced",&value)==ESP_OK&&(value==0||value==1))state.reduced_motion=value;
        if(nvs_get_i32(settings,"wallpaper",&value)==ESP_OK&&value>=0&&value<=2)state.wallpaper_index=value;
    }
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=16384,.rx_buffer_size=4096};
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK)return;
    /* USB protocol storage never goes to LCD DMA; reserve internal RAM for radio/DMA. */
    parser=heap_caps_calloc(1,sizeof(*parser),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    packet=heap_caps_malloc(JX_HEADER+JX_MAX_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    usb_snapshot=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(parser&&packet&&usb_snapshot?ESP_OK:ESP_ERR_NO_MEM);
    physical_display_init();display_worker_init();
    lv_init();lv_tick_set_cb(tick);
    lv_display_t *display=lv_display_create(360,360);lcd_display=display;
    if(!display)return;
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    void *draw=heap_caps_malloc(360*360*2,MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM);
    void *draw2=heap_caps_malloc(360*360*2,MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(draw&&draw2?ESP_OK:ESP_ERR_NO_MEM);
    _Static_assert(LV_DRAW_BUF_STRIDE_ALIGN==1,"LCD expects tightly packed RGB565 rectangles");
    lv_display_set_buffers(display,draw,draw2,360*360*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_display_set_flush_wait_cb(display,flush_wait);
    lv_timer_set_period(lv_display_get_refr_timer(display),16);
    lv_display_add_event_cb(display,probe_event,LV_EVENT_ALL,NULL);
    lv_obj_remove_style_all(lv_screen_active());lv_obj_remove_flag(lv_screen_active(),LV_OBJ_FLAG_SCROLLABLE);
    badge_ui_create(lv_screen_active(),&state,save_settings);management_init(&state);battery_init();motion_service_init();wifi_service_init(&state);ble_service_init(&state);wallpaper_service_init();
    physical_touch_init();
    lv_indev_t *physical=lv_indev_create();lv_indev_set_type(physical,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(physical,physical_pointer_read);lv_timer_set_period(lv_indev_get_read_timer(physical),10);lv_indev_set_scroll_limit(physical,10);
    badge_ui_control_bind(local_control);badge_ui_connected(true);
    uint32_t battery_tick=0;
    for(;;){
        flush_poll();
        int64_t loop_started=esp_timer_get_time();
        int64_t measured=runtime_begin();
        uint8_t incoming[256];int n=usb_serial_jtag_read_bytes(incoming,sizeof(incoming),0);
        if(n>0)jx_feed(parser,incoming,n,receive,NULL);
        runtime_end(MON_USB,measured);
        measured=runtime_begin();wifi_service_poll();runtime_end(MON_WIFI,measured);
        measured=runtime_begin();ble_service_poll();runtime_end(MON_BLE,measured);
        measured=runtime_begin();motion_service_poll();runtime_end(MON_MOTION,measured);
        measured=runtime_begin();wallpaper_service_poll();runtime_end(MON_WALL,measured);
        uint32_t finished_session;int finished_result;
        if(wallpaper_finish_result(&finished_session,&finished_result)&&attached){
            uint8_t ack[10]={JX_WALL_FINISH,(uint8_t)finished_result};jx_put32(ack+2,finished_session);send_packet(JX_WALL_ACK,ack,sizeof(ack));
        }
        if(tick()-battery_tick>=2000){measured=runtime_begin();battery_update();runtime_end(MON_BATTERY,measured);battery_tick=tick();}
        measured=runtime_begin();management_poll();runtime_end(MON_MANAGEMENT,measured);
        if(bridge&&attached&&tick()-bridge_seen>5000){attached=false;wallpaper_cancel(1,0);}
        if(monitor_scene>=0){if(!attached||tick()-bridge_seen>5000){monitor_scene=-1;badge_ui_probe_restore(0);}else badge_ui_probe_scene(monitor_scene,tick()-monitor_epoch);}
        probe_poll();runtime_poll();measured=runtime_begin();lv_timer_handler();runtime_end(MON_GUI,measured);flush_poll();if(extra&&loop_started){uint32_t us=(uint32_t)(esp_timer_get_time()-loop_started);if(us>extra->loop_max)extra->loop_max=us;}physical_display_backlight(state.brightness,badge_ui_is_asleep()||(probing&&!probe_lcd));gui_wait(loop_started);
    }
}
