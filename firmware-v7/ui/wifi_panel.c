#include "badge_ui.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t *wall_panel,*wall_qr,*wall_address,*wall_message,*wall_ap_label;
static void (*wall_hotspot)(void);
static char wall_url[128];
static lv_obj_t *management_button,*management_code;
static void (*management_action)(void);
void badge_management_bind(void (*callback)(void)){management_action=callback;}
void badge_management_toggle(void){if(management_action)management_action();}
static void management_clicked(lv_event_t *e){(void)e;badge_management_toggle();}
void badge_management_update(bool active,const char *code,unsigned remaining){
    if(!management_button)return;
    lv_label_set_text(lv_obj_get_child(management_button,0),active?"关闭手机管理":"允许手机管理");
    lv_label_set_text_fmt(management_code,active?"%.*s\n%s\n剩余 %u 秒":"在设备上授权后使用",16,code,strlen(code)>16?code+16:"",remaining);
    if(active&&! *wall_url)lv_obj_remove_flag(management_code,LV_OBJ_FLAG_HIDDEN);else if(active)lv_obj_add_flag(management_code,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(management_code,LV_OBJ_FLAG_HIDDEN);
}
void badge_wallpaper_bind(void (*callback)(void)){wall_hotspot=callback;}
void badge_wallpaper_hotspot(void){if(wall_hotspot)wall_hotspot();}
void badge_wallpaper_hide(void){if(wall_panel)lv_obj_add_flag(wall_panel,LV_OBJ_FLAG_HIDDEN);}
static void wall_back(lv_event_t *e){(void)e;badge_ui_external_panel(wall_panel,false);}
static void wall_toggle(lv_event_t *e){(void)e;badge_wallpaper_hotspot();}
void badge_wallpaper_open(void){badge_wifi_hide();badge_ble_hide();badge_ui_external_panel(wall_panel,true);}

static badge_state_t *state;
static badge_wifi_actions_t actions;
static lv_obj_t *panel,*network_list,*note,*toggle,*sheet,*password,*keyboard,*sheet_note;
static badge_wifi_ap_t networks[12];
static int network_count;
static char selected[33];
static bool selected_secure;
static const char *lower[]={"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l","\n","ABC","z","x","c","v","b","n","m","Del","\n","123","-","_","Space",".","OK",""};
static const char *upper[]={"Q","W","E","R","T","Y","U","I","O","P","\n","A","S","D","F","G","H","J","K","L","\n","abc","Z","X","C","V","B","N","M","Del","\n","123","-","_","Space",".","OK",""};
static const char *symbols[]={"1","2","3","4","5","6","7","8","9","0","\n","!","@","#","$","%","^","&","*","(",")","\n","abc","-","_","=","+","?",":","/","Del","\n","ABC",",",".","Space","OK",""};

static lv_obj_t *surface(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int radius){
    lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *value,int x,int y,int width,const lv_font_t *font,uint32_t color){
    lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,value);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *value,int x,int y,int w,int h,lv_event_cb_t callback){
    lv_obj_t *o=surface(p,x,y,w,h,UI_SURFACE,16);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(UI_SELECTED),LV_STATE_PRESSED);
    lv_obj_t *l=label(o,value,0,0,w,&font14,UI_TEXT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);
    if(!strcmp(value,"‹")){lv_obj_set_style_text_font(l,&font24,0);lv_obj_set_style_bg_opa(o,0,0);}
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);
    if(callback)lv_obj_add_event_cb(o,callback,LV_EVENT_CLICKED,NULL);
    return o;
}
void badge_wallpaper_create(lv_obj_t *parent){
    wall_panel=surface(parent,0,0,360,360,UI_BG,180);lv_obj_set_style_clip_corner(wall_panel,true,0);
    button(wall_panel,"‹",85,36,44,44,wall_back);label(wall_panel,"手机管理",132,36,152,&font24,UI_TEXT);
    wall_message=label(wall_panel,"连接 Wi-Fi 或开启直连热点",57,74,246,&font14,UI_MUTED);
    lv_obj_set_style_text_align(wall_message,LV_TEXT_ALIGN_CENTER,0);
    wall_qr=lv_qrcode_create(wall_panel);lv_qrcode_set_size(wall_qr,136);lv_obj_set_pos(wall_qr,112,97);
    lv_qrcode_set_quiet_zone(wall_qr,true);
    lv_obj_set_style_border_width(wall_qr,6,0);lv_obj_set_style_border_color(wall_qr,lv_color_white(),0);
    wall_address=label(wall_panel,"",66,251,228,&font14,UI_TEXT);lv_obj_set_style_text_align(wall_address,LV_TEXT_ALIGN_CENTER,0);
    management_code=label(wall_panel,"在设备上授权后使用",72,139,216,&font14,UI_TEXT);lv_obj_set_style_text_align(management_code,LV_TEXT_ALIGN_CENTER,0);
    management_button=button(wall_panel,"允许手机管理",70,270,106,44,management_clicked);
    lv_obj_t *ap=button(wall_panel,"开启直连热点",184,270,106,44,wall_toggle);wall_ap_label=lv_obj_get_child(ap,0);
    lv_obj_add_flag(wall_qr,LV_OBJ_FLAG_HIDDEN);badge_wallpaper_hide();
}
void badge_wallpaper_update(const char *url,bool hotspot,const char *name,const char *password,const char *message){
    if(!wall_panel)return;
    if(strcmp(url,wall_url)){
        snprintf(wall_url,sizeof(wall_url),"%s",url);
        if(*url&&lv_qrcode_update(wall_qr,url,strlen(url))==LV_RESULT_OK)lv_obj_remove_flag(wall_qr,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(wall_qr,LV_OBJ_FLAG_HIDDEN);
        if(*url)lv_obj_add_flag(management_code,LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(wall_ap_label,hotspot?"关闭直连热点":"开启直连热点");
    if(hotspot){lv_label_set_text_fmt(wall_message,"%s",name);lv_label_set_text_fmt(wall_address,"密码 %s",password);}
    else {lv_label_set_text(wall_message,*message?message:*url?"手机扫码，选择照片":"先连接 Wi-Fi");
        char address[40];size_t n=strcspn(url,"#");snprintf(address,sizeof(address),"%.*s",(int)n,url);lv_label_set_text(wall_address,address);}
}
static void cancel(lv_event_t *e){
    (void)e;lv_textarea_set_text(password,"");lv_obj_add_flag(sheet,LV_OBJ_FLAG_HIDDEN);
}
static void submit(lv_event_t *e){
    (void)e;const char *value=lv_textarea_get_text(password);size_t n=strlen(value);
    if(selected_secure&&(n<8||n>63)){lv_label_set_text(sheet_note,"密码需要 8～63 位");return;}
    for(size_t i=0;i<n;i++)if((unsigned char)value[i]<32||(unsigned char)value[i]>126){lv_label_set_text(sheet_note,"请使用英文、数字或符号");return;}
    if(!actions.connect){lv_label_set_text(sheet_note,"离线预览，连接需在实板运行");return;}
    char copy[64];memcpy(copy,value,n+1);
    badge_ui_wifi_message("正在连接…");
    actions.connect(selected,selected_secure?copy:"");
    memset(copy,0,sizeof(copy));cancel(NULL);
}
void badge_ui_wifi_text(const char *value){
    if(!sheet||lv_obj_has_flag(sheet,LV_OBJ_FLAG_HIDDEN))return;
    for(const unsigned char *p=(const unsigned char*)value;*p;p++)if(*p<32||*p>126)return;
    lv_textarea_add_text(password,value);
}
void badge_ui_wifi_key(int key){
    if(!sheet||lv_obj_has_flag(sheet,LV_OBJ_FLAG_HIDDEN))return;
    if(key==8)lv_textarea_delete_char(password);
    if(key==13)submit(NULL);
    if(key==27)cancel(NULL);
}
static void key_pressed(lv_event_t *e){
    uint32_t id=lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    const char *value=lv_buttonmatrix_get_button_text(keyboard,id);if(!value)return;
    if(!strcmp(value,"Del"))badge_ui_wifi_key(8);
    else if(!strcmp(value,"OK"))submit(NULL);
    else if(!strcmp(value,"abc"))lv_buttonmatrix_set_map(keyboard,lower);
    else if(!strcmp(value,"ABC"))lv_buttonmatrix_set_map(keyboard,upper);
    else if(!strcmp(value,"123"))lv_buttonmatrix_set_map(keyboard,symbols);
    else badge_ui_wifi_text(!strcmp(value,"Space")?" ":value);
}
static void select_network(lv_event_t *e){
    int index=(int)(intptr_t)lv_event_get_user_data(e);if(index<0||index>=network_count)return;
    if(networks[index].security==2){badge_ui_wifi_message("此网络需企业账号认证，暂不支持");return;}
    snprintf(selected,sizeof(selected),"%s",networks[index].ssid);selected_secure=networks[index].security!=0;
    lv_label_set_text(lv_obj_get_child(sheet,0),selected);
    lv_label_set_text(sheet_note,selected_secure?"输入密码":"开放网络，无需密码");
    lv_textarea_set_text(password,"");lv_buttonmatrix_set_map(keyboard,lower);
    if(selected_secure){lv_obj_remove_flag(password,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(keyboard,LV_OBJ_FLAG_HIDDEN);}
    else {lv_obj_add_flag(password,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);}
    lv_obj_remove_flag(sheet,LV_OBJ_FLAG_HIDDEN);
}
void badge_ui_wifi_results(const badge_wifi_ap_t *aps,int count){
    if(!panel)return;
    network_count=count<0?0:count>12?12:count;
    if(network_count)memcpy(networks,aps,network_count*sizeof(*aps));
    lv_obj_clean(network_list);
    for(int i=0;i<network_count;i++){
        networks[i].ssid[32]=0;
        lv_obj_t *row=surface(network_list,0,i*66,256,60,UI_BG,0);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,badge_ui_click_guard,LV_EVENT_ALL,NULL);
        lv_obj_set_style_bg_color(row,lv_color_hex(UI_SELECTED),LV_STATE_PRESSED);
        lv_obj_add_event_cb(row,select_network,LV_EVENT_CLICKED,(void*)(intptr_t)i);
        bool connected=state->wifi_connected==1&&!strcmp(state->wifi_ssid,networks[i].ssid);
        if(connected)lv_obj_set_style_bg_color(row,lv_color_hex(UI_SELECTED),0);
        label(row,networks[i].ssid,14,9,228,&font18,UI_TEXT);
        char detail[72];
        snprintf(detail,sizeof(detail),"%s · %d dBm",connected?"已连接":networks[i].security==2?"企业认证":networks[i].security?"需要密码":"开放网络",networks[i].rssi);
        label(row,detail,14,34,228,&font14,UI_MUTED);
    }
    lv_obj_scroll_to_y(network_list,0,LV_ANIM_OFF);
    badge_ui_wifi_message(network_count?"附近的网络":"没有发现网络，点刷新重试");
}
void badge_ui_wifi_message(const char *message){if(note)lv_label_set_text(note,message);}
void badge_ui_wifi_bind(const badge_wifi_actions_t *value){actions=value?*value:(badge_wifi_actions_t){0};}
static void scan(lv_event_t *e){
    (void)e;
    if(!state->wifi_enabled){badge_ui_wifi_message("Wi-Fi 已关闭");return;}
    if(actions.scan){badge_ui_wifi_message("正在扫描附近的网络…");actions.scan();}
    else if(state->live){badge_ui_wifi_message("此固件未提供 Wi-Fi 配置接口");}
    else {
        const badge_wifi_ap_t examples[]={ {.ssid="Egger",.rssi=-35,.security=1},{.ssid="CABadge-Lab",.rssi=-62,.security=1},{.ssid="Guest",.rssi=-74,.security=0} };
        badge_ui_wifi_results(examples,3);badge_ui_wifi_message("示例列表 · 离线预览");
    }
}
static void enabled(lv_event_t *e){
    bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    if(actions.enable){badge_ui_wifi_message("正在切换 Wi-Fi…");actions.enable(on);}
    else if(!state->live){state->wifi_enabled=on;if(on)scan(NULL);else lv_obj_clean(network_list);}
    else {badge_wifi_refresh();badge_ui_wifi_message("请先连接设备");}
}
void badge_wifi_request_enable(bool on){if(actions.enable)actions.enable(on);else if(!state->live){state->wifi_enabled=on;badge_ui_refresh();}else badge_ui_notice("请先连接设备");}
static void close_panel(lv_event_t *e){(void)e;cancel(NULL);badge_ui_external_panel(panel,false);}
void badge_wifi_open(void){badge_ble_hide();badge_wallpaper_hide();badge_ui_external_panel(panel,true);badge_wifi_refresh();scan(NULL);}
void badge_wifi_hide(void){if(panel)lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);if(sheet)cancel(NULL);}
bool badge_ui_wifi_visible(void){return panel&&!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN);}
void badge_wifi_refresh(void){
    if(!toggle)return;
    badge_liquid_set(toggle,state->wifi_enabled,state->reduced_motion);
}
void badge_wifi_create(lv_obj_t *parent,badge_state_t *value){
    state=value;panel=surface(parent,0,0,360,360,UI_BG,180);lv_obj_set_style_clip_corner(panel,true,0);
    button(panel,"‹",85,36,44,44,close_panel);label(panel,"Wi-Fi",140,40,86,&font24,UI_TEXT);
    toggle=badge_liquid_create(panel,228,48,enabled);
    note=label(panel,"",52,78,256,&font14,UI_MUTED);
    network_list=surface(panel,52,106,256,186,UI_BG,0);lv_obj_add_flag(network_list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(network_list,LV_OBJ_FLAG_SCROLL_CHAIN);lv_obj_set_scroll_dir(network_list,LV_DIR_VER);lv_obj_set_scrollbar_mode(network_list,LV_SCROLLBAR_MODE_OFF);
    button(panel,"刷新列表",123,300,114,44,scan);
    sheet=surface(panel,0,0,360,360,UI_BG,180);lv_obj_set_style_clip_corner(sheet,true,0);
    lv_obj_t *title=label(sheet,"",90,27,180,&font18,UI_TEXT);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    sheet_note=label(sheet,"",48,55,264,&font14,UI_MUTED);
    password=lv_textarea_create(sheet);lv_obj_set_pos(password,48,84);lv_obj_set_size(password,264,42);
    lv_obj_set_style_text_font(password,&font18,0);lv_obj_set_style_text_color(password,lv_color_hex(UI_TEXT),0);
    lv_obj_set_style_bg_color(password,lv_color_hex(UI_SURFACE),0);lv_obj_set_style_bg_opa(password,255,0);
    lv_obj_set_style_radius(password,12,0);lv_obj_set_style_pad_all(password,8,0);
    lv_textarea_set_one_line(password,true);lv_textarea_set_max_length(password,63);
    lv_textarea_set_password_mode(password,true);lv_textarea_set_password_show_time(password,0); /* • */
    button(sheet,"取消",70,129,103,44,cancel);button(sheet,"连接",187,129,103,44,submit);
    keyboard=lv_buttonmatrix_create(sheet);lv_obj_remove_style_all(keyboard);lv_obj_set_pos(keyboard,58,178);lv_obj_set_size(keyboard,244,126);
    lv_obj_set_style_text_font(keyboard,&font14,LV_PART_ITEMS);lv_obj_set_style_text_color(keyboard,lv_color_hex(UI_TEXT),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(UI_SURFACE),LV_PART_ITEMS);lv_obj_set_style_bg_opa(keyboard,255,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(UI_ACCENT),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_radius(keyboard,5,LV_PART_ITEMS);lv_obj_set_style_pad_column(keyboard,3,0);lv_obj_set_style_pad_row(keyboard,4,0);
    lv_buttonmatrix_set_map(keyboard,lower);lv_obj_add_event_cb(keyboard,key_pressed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_flag(sheet,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
}

/* The Bluetooth settings page shares the same round-screen list controls. */
static badge_ble_actions_t ble_actions;
static badge_ble_info_t ble_info;
bool badge_ble_enabled(void){return ble_info.enabled;}
const char *badge_ble_summary(void){
    return !ble_info.enabled?"已关闭":ble_info.connecting?"连接中":ble_info.scanning?"扫描中":
        ble_info.inbound&&ble_info.outbound?"手机与外设已连接":ble_info.inbound?"手机已连接":
        ble_info.outbound?"外设已连接":ble_info.advertising?"可被发现":"已开启";
}
static lv_obj_t *ble_panel,*ble_toggle,*ble_note,*ble_list,*ble_scan_button;
static void ble_render(void);
void badge_ui_ble_bind(const badge_ble_actions_t *value){ble_actions=value?*value:(badge_ble_actions_t){0};}
void badge_ui_ble_message(const char *value){if(ble_note)lv_label_set_text(ble_note,value);}
static void ble_close(lv_event_t *e){(void)e;badge_ui_external_panel(ble_panel,false);}
void badge_ble_request_toggle(void){if(ble_actions.enable)ble_actions.enable(!ble_info.enabled);else if(!state->live){ble_info.enabled=!ble_info.enabled;ble_info.advertising=ble_info.enabled;ble_render();badge_ui_refresh();}else badge_ui_notice("请先连接设备");}
static void ble_enable(lv_event_t *e){
    bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    if(ble_actions.enable){badge_ui_ble_message("正在切换蓝牙…");ble_actions.enable(on);}
    else if(!state->live){ble_info.enabled=on;ble_info.advertising=on;if(!on){ble_info.inbound=ble_info.outbound=false;ble_info.count=0;}ble_render();}
    else {ble_render();badge_ui_ble_message("此固件未提供蓝牙配置接口");}
}
static void ble_scan(lv_event_t *e){
    (void)e;
    if(!ble_info.enabled){badge_ui_ble_message("请先打开蓝牙");return;}
    if(ble_actions.scan)ble_actions.scan();
    else if(!state->live){
        ble_info.count=2;
        ble_info.devices[0]=(badge_ble_device_t){.name="BLE Sensor",.rssi=-48,.connectable=true,.address={1,2,3,4,5,6}};
        ble_info.devices[1]=(badge_ble_device_t){.name="Beacon",.rssi=-67,.address={2,2,3,4,5,6}};
        ble_render();badge_ui_ble_message("示例列表 · 离线预览");
    }else badge_ui_ble_message("此固件未提供蓝牙配置接口");
}
static void ble_connect(lv_event_t *e){
    int i=(int)(intptr_t)lv_event_get_user_data(e);
    if(i<0||i>=ble_info.count||!ble_info.enabled)return;
    if(!ble_info.devices[i].connectable){badge_ui_ble_message("此设备仅广播，不接受连接");return;}
    if(ble_info.outbound){badge_ui_ble_message("请先断开本机已连接的设备");return;}
    if(ble_info.connecting||ble_info.scanning)return;
    if(ble_actions.connect)ble_actions.connect(&ble_info.devices[i]);
    else badge_ui_ble_message("离线预览，连接需在实板运行");
}
static void ble_disconnect(lv_event_t *e){
    if(ble_actions.disconnect)ble_actions.disconnect((bool)(intptr_t)lv_event_get_user_data(e));
    else badge_ui_ble_message("离线预览，断开需在实板运行");
}
static void ble_render(void){
    if(!ble_panel)return;
    badge_liquid_set(ble_toggle,ble_info.enabled,state->reduced_motion);
    bool busy=!ble_info.enabled||ble_info.scanning||ble_info.connecting;
    if(busy)lv_obj_add_state(ble_scan_button,LV_STATE_DISABLED);else lv_obj_remove_state(ble_scan_button,LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(ble_scan_button,0),ble_info.scanning?"扫描中…":"扫描附近设备");
    const char *status=ble_info.error?"操作失败，请重试或重启蓝牙":!ble_info.enabled?"蓝牙已关闭":ble_info.connecting?"正在连接设备…":ble_info.scanning?"正在扫描附近 BLE 设备…":ble_info.advertising?"手机可发现 CABadge":ble_info.inbound?"对方已连接本机":"蓝牙已打开";
    badge_ui_ble_message(status);
    lv_obj_clean(ble_list);int y=0;
    if(ble_info.enabled){
        label(ble_list,"本机 · CABadge",10,y,236,&font18,UI_TEXT);y+=30;
        label(ble_list,"连接到本机的设备",10,y,236,&font14,UI_MUTED);y+=26;
        if(ble_info.inbound){
            lv_obj_t *row=button(ble_list,ble_info.inbound_address,0,y,256,44,NULL);
            lv_obj_set_style_bg_color(row,lv_color_hex(UI_SELECTED),0);
            lv_obj_add_event_cb(row,ble_disconnect,LV_EVENT_CLICKED,(void*)0);y+=50;
            label(ble_list,"已连接 · 点按断开",10,y,236,&font14,UI_MUTED);y+=26;
        }else {label(ble_list,"等待手机连接",10,y,236,&font14,UI_MUTED);y+=30;}
        if(ble_info.outbound){
            label(ble_list,"本机连接的设备 · 点按断开",10,y,236,&font14,UI_MUTED);y+=26;
            lv_obj_t *row=button(ble_list,*ble_info.outbound_name?ble_info.outbound_name:ble_info.outbound_address,0,y,256,44,NULL);
            lv_obj_set_style_bg_color(row,lv_color_hex(UI_SELECTED),0);
            lv_obj_add_event_cb(row,ble_disconnect,LV_EVENT_CLICKED,(void*)1);y+=52;
        }
        label(ble_list,"附近的 BLE 设备",10,y,236,&font14,UI_MUTED);y+=26;
        if(!ble_info.count){label(ble_list,ble_info.scanning?"扫描中，请稍候":"暂无设备",10,y,236,&font14,UI_MUTED);}
        for(int i=0;i<ble_info.count;i++){
            const badge_ble_device_t *d=&ble_info.devices[i];char detail[80];
            lv_obj_t *row=surface(ble_list,0,y,256,64,UI_BG,0);y+=70;
            lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,badge_ui_click_guard,LV_EVENT_ALL,NULL);
            lv_obj_set_style_bg_color(row,lv_color_hex(UI_SELECTED),LV_STATE_PRESSED);
            lv_obj_add_event_cb(row,ble_connect,LV_EVENT_CLICKED,(void*)(intptr_t)i);
            label(row,*d->name?d->name:"未命名设备",12,8,232,&font18,UI_TEXT);
            snprintf(detail,sizeof(detail),"%s · %d dBm",d->connectable?"点按连接":"仅广播",d->rssi);
            label(row,detail,12,36,232,&font14,UI_MUTED);
        }
    }
}
void badge_ui_ble_update(const badge_ble_info_t *value){
    ble_info=*value;if(ble_info.count<0)ble_info.count=0;if(ble_info.count>BADGE_BLE_DEVICES)ble_info.count=BADGE_BLE_DEVICES;
    ble_info.inbound_address[17]=ble_info.outbound_address[17]=ble_info.outbound_name[32]=0;
    for(int i=0;i<ble_info.count;i++)ble_info.devices[i].name[32]=0;
    if(badge_ui_ble_visible())ble_render();
}
void badge_ble_open(void){
    badge_wifi_hide();badge_wallpaper_hide();badge_ui_external_panel(ble_panel,true);ble_render();lv_obj_scroll_to_y(ble_list,0,LV_ANIM_OFF);
    if(!ble_actions.scan&&!state->live)badge_ui_ble_message("离线预览 · 需烧录后使用蓝牙");
}
void badge_ble_hide(void){if(ble_panel)lv_obj_add_flag(ble_panel,LV_OBJ_FLAG_HIDDEN);}
bool badge_ui_ble_visible(void){return ble_panel&&!lv_obj_has_flag(ble_panel,LV_OBJ_FLAG_HIDDEN);}
void badge_ble_create(lv_obj_t *parent,badge_state_t *value){
    state=value;ble_info.enabled=true;ble_info.advertising=!state->live;
    ble_panel=surface(parent,0,0,360,360,UI_BG,180);lv_obj_set_style_clip_corner(ble_panel,true,0);
    button(ble_panel,"‹",85,36,44,44,ble_close);label(ble_panel,"蓝牙",140,40,86,&font24,UI_TEXT);
    ble_toggle=badge_liquid_create(ble_panel,228,48,ble_enable);
    ble_note=label(ble_panel,"",52,78,256,&font14,UI_MUTED);
    ble_list=surface(ble_panel,52,108,256,186,UI_BG,0);lv_obj_add_flag(ble_list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ble_list,LV_OBJ_FLAG_SCROLL_CHAIN);lv_obj_set_scroll_dir(ble_list,LV_DIR_VER);lv_obj_set_scrollbar_mode(ble_list,LV_SCROLLBAR_MODE_OFF);
    ble_scan_button=button(ble_panel,"扫描附近设备",114,298,132,44,ble_scan);
    lv_obj_add_flag(ble_panel,LV_OBJ_FLAG_HIDDEN);
}

static void panel_x(void *obj,int32_t x){lv_obj_set_x(obj,x);}
static void panel_closed(lv_anim_t *a){lv_obj_add_flag(a->var,LV_OBJ_FLAG_HIDDEN);}
void badge_panel_slide(lv_obj_t *p,bool open,bool reduced){
    bool hidden=lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN);int start=hidden?360:lv_obj_get_x(p);
    lv_anim_delete(p,panel_x);
    if(reduced){lv_obj_set_x(p,0);if(open){lv_obj_remove_flag(p,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p);}else lv_obj_add_flag(p,LV_OBJ_FLAG_HIDDEN);return;}
    if(!open&&hidden)return;
    lv_obj_remove_flag(p,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,p);lv_anim_set_exec_cb(&a,panel_x);
    lv_anim_set_values(&a,start,open?0:360);lv_anim_set_duration(&a,220);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    if(!open)lv_anim_set_completed_cb(&a,panel_closed);
    lv_anim_start(&a);
}
