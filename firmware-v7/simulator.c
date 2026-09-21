#include "ui/badge_ui.h"
#include "bridge.h"
#include "usb_screen/bridge_protocol.h"
#include "src/misc/cache/lv_cache.h"
#include <SDL.h>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static lv_display_t *display;
static uint16_t pixels[360*360],drawbuf[360*16];
static badge_state_t state={.brightness=100,.battery_mv=-1,.wifi_connected=-1,.ble_connected=-1,.wifi_rssi=-127,.live=true};
static int mx,my;
static bool pressed,done,offline,test_mode,cancel_pointer;
static unsigned frames,frame_bytes,total_bytes,record_index;
static double next_frame,frame_ms=1000.0/30,qspi_mhz=40,render_ms;
static const char *record_dir;
static FILE *record_manifest;
static double record_at;
static PROCESS_INFORMATION importer;
static lv_image_dsc_t imported;
static void *import_pixels;
static double milliseconds(void){return (double)SDL_GetPerformanceCounter()*1000.0/SDL_GetPerformanceFrequency();}
static double budget(unsigned bytes){return fmax(frame_ms,bytes*8.0/(qspi_mhz*4000.0)+render_ms);}
static void screenshot(const char *name){
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormatFrom(pixels,360,360,16,720,SDL_PIXELFORMAT_RGB565);
    CHECK(s);CHECK(SDL_SaveBMP(s,name)==0);SDL_FreeSurface(s);
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){
    int w=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;y++)memcpy(pixels+y*360+a->x1,data+(y-a->y1)*w*2,w*2);
    frame_bytes+=w*(a->y2-a->y1+1)*2;
    lv_display_flush_ready(d);
}
static void pointer(lv_indev_t *i,lv_indev_data_t *d){
    bool inside=(mx-180)*(mx-180)+(my-180)*(my-180)<=180*180;
    if(cancel_pointer||(pressed&&!inside)){lv_indev_reset(i,NULL);lv_indev_wait_release(i);cancel_pointer=false;}
    d->point.x=mx;d->point.y=my;d->state=pressed&&inside?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void refresh_scheduled(lv_timer_t *timer){lv_timer_pause(timer);}
static void tick(void){
    lv_timer_handler();if(!offline)bridge_poll();double now=milliseconds();
    if(now>=next_frame){
        frame_bytes=0;lv_refr_now(display);
        /* Bandwidth floor, not a claim to emulate the ESP32's software rasterizer. */
        next_frame=now+budget(frame_bytes);
        if(frame_bytes){frames++;total_bytes+=frame_bytes;SDL_UpdateTexture(texture,NULL,pixels,720);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);}
        if(record_dir){
            if(record_index)fprintf(record_manifest,"file 'frame-%05u.bmp'\nduration %.6f\n",record_index-1,(now-record_at)/1000.0);
            char file[1024];snprintf(file,sizeof(file),"%s/frame-%05u.bmp",record_dir,record_index++);screenshot(file);record_at=now;
        }
    }
    SDL_Delay(1);
}
static void wait_ms(int ms){double end=milliseconds()+ms;do{tick();}while(milliseconds()<end);}
static void click(int x,int y){mx=x;my=y;pressed=true;wait_ms(45);pressed=false;wait_ms(75);}
static void swipe(int x,int y,int dx,int dy){mx=x;my=y;pressed=true;wait_ms(40);for(int i=1;i<=24;i++){mx=x+dx*i/24;my=y+dy*i/24;wait_ms(12);}pressed=false;wait_ms(500);}
static void shot(const char *name){char file[256];snprintf(file,sizeof(file),"build/%s.bmp",name);wait_ms(record_dir?500:80);screenshot(file);}
static bool load_photo(const char *path){
    SDL_Surface *input=SDL_LoadBMP(path);if(!input)return false;
    SDL_Surface *rgb=SDL_ConvertSurfaceFormat(input,SDL_PIXELFORMAT_RGB565,0);SDL_FreeSurface(input);
    if(!rgb||rgb->w!=360||rgb->h!=360){if(rgb)SDL_FreeSurface(rgb);return false;}
    uint8_t *data=malloc(259200);if(!data){SDL_FreeSurface(rgb);return false;}
    for(int y=0;y<360;y++)memcpy(data+y*720,(uint8_t*)rgb->pixels+y*rgb->pitch,720);SDL_FreeSurface(rgb);
    if(!offline){bool ok=bridge_upload(data,259200);free(data);return ok;}
    lv_image_cache_drop(&imported);void *old=import_pixels;import_pixels=data;
    imported=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720},.data_size=259200,.data=data};
    badge_ui_restore_photo(&imported);free(old);badge_ui_page(2,true);return true;
}
static void import_photo(const char *file){
    if(importer.hProcess)return;wchar_t path[16000],cmd[32768];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,file,-1,path,16000)||wcschr(path,L'"'))return;
    swprintf(cmd,32768,L"\".venv\\Scripts\\python.exe\" import_photo.py \"%ls\"",path);
    STARTUPINFOW si={.cb=sizeof(si)};
    if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&importer))badge_ui_notice("无法启动图片处理");
}
static void poll_import(void){
    if(!importer.hProcess||WaitForSingleObject(importer.hProcess,0)!=WAIT_OBJECT_0)return;DWORD result;
    GetExitCodeProcess(importer.hProcess,&result);CloseHandle(importer.hThread);CloseHandle(importer.hProcess);memset(&importer,0,sizeof(importer));
    if(result||!load_photo("build/imported.bmp"))badge_ui_notice("图片导入失败");
}
static void noop(lv_event_t *e){(void)e;}
static void self_test(void){
    CHECK(fabs(budget(259200)-1000.0/30)<.001);qspi_mhz=10;CHECK(fabs(budget(259200)-51.84)<.001);qspi_mhz=40;
    CHECK(badge_ui_gallery_clean());shot("v7-01-wallpaper");
    swipe(290,180,-220,0);CHECK(badge_ui_current_page()==7);shot("v7-02-member");
    swipe(85,180,210,0);CHECK(badge_ui_current_page()==0);
    swipe(180,65,0,220);int drawer;badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer==-1000);shot("v7-03-control");
    swipe(180,325,0,-245);badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer==0);
    swipe(180,280,0,-210);CHECK(badge_ui_current_page()==1);shot("v7-04-functions");
    click(180,155);wait_ms(500);CHECK(badge_ui_current_page()==2);shot("v7-05-carousel");
    int selected;float before,after;badge_ui_test_state(NULL,NULL,&selected,&before);CHECK(selected==0);
    swipe(250,173,-135,0);badge_ui_test_state(NULL,NULL,&selected,&after);CHECK(selected==1&&fabsf(after-before)>.9f);
    CHECK(state.wallpaper_index==0);shot("v7-06-carousel-next");
    click(180,300);wait_ms(200);CHECK(badge_ui_current_page()==2);wait_ms(1400);CHECK(state.wallpaper_index==1&&badge_ui_current_page()==0);shot("v7-07-applied");
    /* An opening drawer can be grabbed and reversed without a position reset. */
    badge_ui_page(8,true);wait_ms(65);badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer<0&&drawer>-1000);
    mx=180;my=100;pressed=true;wait_ms(30);for(int i=1;i<=10;i++){my=100-i*8;wait_ms(12);}pressed=false;wait_ms(500);
    badge_ui_page(7,false);badge_ui_sleep(true);badge_ui_motion();CHECK(!badge_ui_is_asleep()&&badge_ui_current_page()==7);
    badge_ui_motion();CHECK(badge_ui_current_page()==7);
    badge_ui_settings(100,true);badge_ui_page(8,true);badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer==-1000);badge_ui_settings(100,false);
    badge_ui_page(4,false);shot("v7-08-connections");badge_wifi_open();wait_ms(350);shot("v7-09-wifi");bool was_on=state.wifi_enabled;click(252,62);CHECK(state.wifi_enabled!=was_on);shot("v7-liquid-toggle");click(252,62);CHECK(state.wifi_enabled==was_on);badge_wifi_hide();badge_ble_open();wait_ms(350);shot("v7-10-ble");badge_ble_hide();
    /* Reduced motion must still put external panels above the connections page. */
    badge_ui_settings(100,true);
    for(int panel=0;panel<3;panel++){
        badge_ui_page(4,false);
        click(panel==0?113:panel==1?247:180,panel==2?252:150);
        if(panel==0)CHECK(badge_ui_wifi_visible());
        if(panel==1)CHECK(badge_ui_ble_visible());
        click(105,58);
        CHECK(badge_ui_current_page()==4);
        CHECK(!badge_ui_wifi_visible()&&!badge_ui_ble_visible());
    }
    badge_ui_settings(100,false);
    lv_obj_t *liquid=badge_liquid_create(lv_screen_active(),10,10,noop),*knob=lv_obj_get_child(liquid,0);
    badge_liquid_set(liquid,true,false);wait_ms(60);int middle=lv_obj_get_x(knob);CHECK(middle>3&&middle<23);CHECK(lv_obj_get_width(knob)<=24);
    badge_liquid_set(liquid,false,false);CHECK(lv_obj_get_x(knob)==middle);wait_ms(200);CHECK(lv_obj_get_x(knob)==3&&lv_obj_get_width(knob)==22);
    badge_liquid_set(liquid,true,true);lv_obj_update_layout(liquid);CHECK(lv_obj_get_x(knob)==23&&lv_obj_get_width(knob)==22);lv_obj_delete(liquid);
    for(int scene=0;scene<3;scene++){unsigned first=frames;double began=milliseconds();while(milliseconds()-began<1300){badge_ui_probe_scene(scene,(unsigned)(milliseconds()-began));tick();}CHECK(frames-first>12);}
    CHECK(state.wallpaper_index==1);badge_ui_probe_restore(0);
    badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer==0&&badge_ui_gallery_clean());
    badge_ui_page(8,false);wait_ms(60);mx=280;my=236;pressed=true;wait_ms(40);
    for(int i=1;i<=12;i++){mx=280-i*10;wait_ms(16);}pressed=false;wait_ms(200);
    CHECK(state.brightness>=40&&state.brightness<=55);int settled=state.brightness;wait_ms(300);CHECK(state.brightness==settled);
    shot("v7-slosh-settled");badge_ui_settings(100,false);
    /* Release after a drag must not turn into a click, even after reversing. */
    badge_ui_page(1,false);wait_ms(80);mx=180;my=155;pressed=true;wait_ms(30);
    mx=210;wait_ms(40);mx=180;wait_ms(40);pressed=false;wait_ms(300);
    CHECK(badge_ui_current_page()==1);
    /* A stationary re-grab must settle again instead of freezing half a page. */
    badge_ui_page(0,false);badge_ui_page(7,true);wait_ms(50);
    click(180,180);wait_ms(500);CHECK(badge_ui_current_page()==7);
    badge_ui_page(0,true);wait_ms(500);CHECK(badge_ui_gallery_clean());
    /* Dominant horizontal diagonal stays horizontal, never opens a drawer. */
    swipe(280,175,-205,38);badge_ui_test_state(NULL,&drawer,NULL,NULL);
    CHECK(drawer==0&&badge_ui_current_page()==7);
    badge_ui_page(0,false);swipe(100,180,45,0);CHECK(badge_ui_current_page()==0);
    /* Native gallery boundaries and interruption; browse never applies a wallpaper. */
    badge_ui_page(2,false);int saved_wall=state.wallpaper_index;
    swipe(180,175,105,0);swipe(180,175,105,0);
    badge_ui_test_state(NULL,NULL,&selected,&after);CHECK(selected==0&&fabsf(after)<.01f);
    click(260,300);wait_ms(40);click(180,175);wait_ms(500);
    badge_ui_test_state(NULL,NULL,&selected,&after);
    CHECK(fabsf(after-roundf(after))<.01f&&state.wallpaper_index==saved_wall);
    /* Input loss cancels a partial drawer; never commits a page or a button. */
    badge_ui_page(0,false);wait_ms(80);mx=180;my=70;pressed=true;wait_ms(30);my=170;wait_ms(60);
    cancel_pointer=true;pressed=false;wait_ms(500);
    badge_ui_test_state(NULL,&drawer,NULL,NULL);CHECK(drawer==0&&badge_ui_current_page()==0);
    /* Returning from nested pages retains the existing parent destinations. */
    badge_ui_page(3,false);click(180,125);wait_ms(400);CHECK(badge_ui_current_page()==5);
    click(105,58);wait_ms(400);CHECK(badge_ui_current_page()==3);
    /* Trust boundary: malformed versions/fields must not change the UI state. */
    bridge_init(&state);int old=state.brightness;
    CHECK(!bridge_snapshot("{}",2));CHECK(!bridge_snapshot("{bad}",5));CHECK(state.brightness==old);
    const char *snapshot="{\"protocol\":1,\"firmware\":\"7.0.0-preview\",\"brightness\":83,\"battery_mv\":4032,\"wifi\":{\"enabled\":true,\"connected\":true,\"ssid\":\"Test only\",\"rssi\":-45,\"networks\":[]},\"ble\":{\"enabled\":true,\"devices\":[]},\"wallpaper\":{\"selected\":1}}";
    CHECK(bridge_snapshot(snapshot,strlen(snapshot)));CHECK(state.brightness==83&&state.battery_mv==4032&&state.wifi_connected==1);
    char trailing[1024];snprintf(trailing,sizeof(trailing),"%s!",snapshot);CHECK(!bridge_snapshot(trailing,strlen(trailing)));
    uint8_t command[]={1,0,0,0,MG_VERSION,BR_APPLY,2};CHECK(br_command_valid(command,sizeof(command)));command[6]=3;CHECK(!br_command_valid(command,sizeof(command)));
    CHECK(bridge_transport_check());
    bridge_close();CHECK(state.wifi_connected==-1&&!bridge_ready());badge_ui_control_bind(NULL);badge_ui_wifi_bind(NULL);badge_ui_ble_bind(NULL);state.live=false;
    badge_ui_settings(100,false);badge_ui_page(7,false);shot("v7-11-final");
    /* The third resource can arrive or disappear while wallpaper paging is open. */
    badge_ui_restore_photo(&badge_ribbons);badge_ui_page(2,false);click(260,300);wait_ms(450);
    badge_ui_test_state(NULL,NULL,&selected,NULL);CHECK(selected==2);CHECK(badge_ui_select_wallpaper(2));
    badge_ui_restore_photo(&badge_wallpaper);badge_ui_test_state(NULL,NULL,&selected,NULL);CHECK(selected==0&&state.wallpaper_index==0);
    badge_ui_page(7,false);
    /* Measure the actual scheduler while continuously invalidating a full frame. */
    unsigned start=frames;double t=milliseconds();while(milliseconds()-t<1500){lv_obj_invalidate(lv_screen_active());tick();}
    double rate=(frames-start)*1000.0/(milliseconds()-t);CHECK(rate<=30.8);CHECK(rate>10);
    qspi_mhz=10;start=frames;t=milliseconds();while(milliseconds()-t<1500){lv_obj_invalidate(lv_screen_active());tick();}
    double slow=(frames-start)*1000.0/(milliseconds()-t);CHECK(slow<=19.9);CHECK(slow>7);qspi_mhz=40;
    printf("Measured conservative 10MHz refresh %.2f FPS.\n",slow);
    printf("PASS UI gestures, wallpaper paging preview/commit, wake-only shake, reduced motion, bridge validation.\nMeasured refresh %.2f FPS; 360x360 RGB565; 40MHz budget (provisional).\n",rate);
}
int main(int argc,char **argv){
    const char *port=NULL;double duration=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--offline"))offline=true;
        else if(!strcmp(argv[i],"--self-test")){test_mode=true;offline=true;}
        else if(!strcmp(argv[i],"--port")&&i+1<argc)port=argv[++i];
        else if(!strcmp(argv[i],"--fps")&&i+1<argc){int fps=atoi(argv[++i]);if(fps<1||fps>60)return 2;frame_ms=1000.0/fps;}
        else if(!strcmp(argv[i],"--qspi")&&i+1<argc){qspi_mhz=atof(argv[++i]);if(qspi_mhz<1||qspi_mhz>62.5)return 2;}
        else if(!strcmp(argv[i],"--render-ms")&&i+1<argc){render_ms=atof(argv[++i]);if(render_ms<0||render_ms>500)return 2;}
        else if(!strcmp(argv[i],"--record")&&i+1<argc)record_dir=argv[++i];
        else if(!strcmp(argv[i],"--duration")&&i+1<argc)duration=atof(argv[++i]);
        else return 2;
    }
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    window=SDL_CreateWindow("NBTCA Badge TOOL - v7",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,720,720,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    CHECK(window);renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);CHECK(renderer);SDL_RenderSetLogicalSize(renderer,360,360);
    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,360,360);CHECK(texture);
    lv_init();lv_tick_set_cb(SDL_GetTicks);display=lv_display_create(360,360);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_timer_set_cb(lv_display_get_refr_timer(display),refresh_scheduled);
    lv_timer_pause(lv_display_get_refr_timer(display));
    lv_obj_remove_style_all(lv_screen_active());lv_obj_remove_flag(lv_screen_active(),LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x141A22),0);lv_obj_set_style_bg_opa(lv_screen_active(),255,0);
    state.live=!offline;badge_ui_create(lv_screen_active(),&state,NULL);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);lv_timer_set_period(lv_indev_get_read_timer(input),10);lv_indev_set_scroll_limit(input,10);
    if(!offline){bridge_init(&state);if(port)bridge_open(port);}
    SDL_StartTextInput();
    if(record_dir){char file[1024];snprintf(file,sizeof(file),"%s/frames.txt",record_dir);record_manifest=fopen(file,"w");CHECK(record_manifest);}
    if(test_mode){self_test();done=true;}
    double start=milliseconds(),title_at=milliseconds();unsigned title_frames=frames;
    while(!done){
        SDL_Event e;while(SDL_PollEvent(&e))switch(e.type){
            case SDL_QUIT:done=true;break;
            case SDL_MOUSEMOTION:mx=e.motion.x;my=e.motion.y;break;
            case SDL_MOUSEBUTTONDOWN:if(e.button.button==SDL_BUTTON_LEFT){mx=e.button.x;my=e.button.y;pressed=true;}break;
            case SDL_MOUSEBUTTONUP:if(e.button.button==SDL_BUTTON_LEFT)pressed=false;break;
            case SDL_WINDOWEVENT:if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){pressed=false;cancel_pointer=true;}break;
            case SDL_TEXTINPUT:badge_ui_wifi_text(e.text.text);break;
            case SDL_DROPFILE:import_photo(e.drop.file);SDL_free(e.drop.file);break;
            case SDL_KEYDOWN:
                if(e.key.keysym.sym==SDLK_F5&&!offline&&port)bridge_open(port);
                else if(e.key.keysym.sym==SDLK_F6){if(offline)badge_ui_motion();else bridge_shake();}
                else if(e.key.keysym.sym==SDLK_F7)badge_ui_request(BADGE_SLEEP,!badge_ui_is_asleep());
                else if(e.key.keysym.sym==SDLK_ESCAPE){if(badge_ui_wifi_visible())badge_ui_wifi_key(27);else badge_ui_page(0,true);}
                else if(e.key.keysym.sym==SDLK_BACKSPACE)badge_ui_wifi_key(8);
                else if(e.key.keysym.sym==SDLK_RETURN)badge_ui_wifi_key(13);
                break;
        }
        poll_import();tick();double now=milliseconds();
        if(now-title_at>500){char title[256];snprintf(title,sizeof(title),"NBTCA Badge TOOL | v7 | %s | PC %.1f FPS (上限 %.0f) | %s %.1f ms / QSPI %.0f MHz 估算",offline?"离线预览":bridge_status(),(frames-title_frames)*1000.0/(now-title_at),1000.0/frame_ms,render_ms>0?"板绘制P95":"未采样",render_ms,qspi_mhz);SDL_SetWindowTitle(window,title);title_at=now;title_frames=frames;}
        if(duration>0&&now-start>duration*1000)done=true;
    }
    if(record_manifest){if(record_index)fprintf(record_manifest,"file 'frame-%05u.bmp'\n",record_index-1);fclose(record_manifest);}
    bridge_close();SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
