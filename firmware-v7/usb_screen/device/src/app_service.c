#include "ui/apps.h"
#include "wallpaper_service.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_memory_utils.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "map_png.h"
#include "ui/ui_transition_cache.h"
#include "physical_display.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#define MAP_ORIGIN "https://bluemap.orangedog.nbtca.space:8001"
#define TILE_PIXELS 128
#define DOWNLOAD_LIMIT 16384
#define PNG_DOWNLOAD_LIMIT (1024*1024)
/* LodePNG's supported custom allocator hook: PNG scratch must not consume DMA RAM. */
void *lodepng_malloc(size_t n){return n<=2100000?heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;}
void *lodepng_realloc(void *p,size_t n){return n<=2100000?heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;}
void lodepng_free(void *p){free(p);}
typedef struct {int world,lod,x,z;int64_t at;uint16_t *pixels;} map_tile_t;
typedef struct {uint64_t dex;int best;} game_save_t;
static QueueHandle_t requests,results,saves;
static TaskHandle_t worker;
static atomic_uint wanted;
static atomic_bool map_busy;
static bool pending_request;
static map_view_t pending_view;
static map_tile_t tiles[9];
static const char *worlds[]={"world","world_the_nether","world_the_end"};
static const float blocks_per_pixel[]={1,2,5,10,25,50};
static void release_tiles(void){for(int i=0;i<9;i++){free(tiles[i].pixels);tiles[i]=(map_tile_t){0};}}
static bool stale(uint32_t serial){return serial!=atomic_load(&wanted);}
static int download(esp_http_client_handle_t client,const char *path,uint8_t *buffer,int cap,uint32_t serial){
    char url[256];snprintf(url,sizeof(url),MAP_ORIGIN "%s",path);
    if(stale(serial)||esp_http_client_set_url(client,url)!=ESP_OK||esp_http_client_open(client,0)!=ESP_OK)return -1;
    int64_t length=esp_http_client_fetch_headers(client);int status=esp_http_client_get_status_code(client),used=0;
    if(status!=200||length>cap){esp_http_client_close(client);return status==404?0:-1;}
    int64_t start=esp_timer_get_time();
    while(used<cap&&!stale(serial)&&esp_timer_get_time()-start<12000000){
        int n=esp_http_client_read(client,(char*)buffer+used,cap-used);
        if(n<0){used=-1;break;}if(!n)break;used+=n;
    }
    bool complete=esp_http_client_is_complete_data_received(client);
    esp_http_client_close(client);return !stale(serial)&&complete?used:-1;
}
static void coordinate_path(char *out,int n){
    char digits[16];snprintf(digits,sizeof(digits),"%d",n);char *p=out;
    for(char *s=digits;*s;s++){*p++=*s;if(*s!='-')*p++='/';}*p=0;
}
static int get_tile(esp_http_client_handle_t client,const map_view_t *view,int lod,int tx,int tz,uint8_t *buffer,int slot){
    for(int i=slot+1;i<9;i++)if(tiles[i].pixels&&tiles[i].world==view->world&&tiles[i].lod==lod&&tiles[i].x==tx&&tiles[i].z==tz){
        map_tile_t old=tiles[slot];tiles[slot]=tiles[i];tiles[i]=old;break;
    }
    map_tile_t *tile=&tiles[slot];int64_t now=esp_timer_get_time();
    if(tile->pixels&&tile->world==view->world&&tile->lod==lod&&tile->x==tx&&tile->z==tz&&now-tile->at<60000000)return 1;
    char x[32],z[32],path[160];coordinate_path(x,tx);coordinate_path(z,tz);z[strlen(z)-1]=0;
    snprintf(path,sizeof(path),"/maps/%s/tiles/%d/x%sz%s.png",worlds[view->world],lod,x,z);
    if(stale(view->serial))return -1;
    char url[256];snprintf(url,sizeof(url),MAP_ORIGIN "%s",path);
    if(esp_http_client_set_url(client,url)!=ESP_OK||esp_http_client_open(client,0)!=ESP_OK){esp_http_client_close(client);return -1;}
    int64_t length=esp_http_client_fetch_headers(client);int status=esp_http_client_get_status_code(client);
    if(status!=200||length>PNG_DOWNLOAD_LIMIT){esp_http_client_close(client);return status==404?0:-1;}
    uint16_t *pixels=heap_caps_malloc(TILE_PIXELS*TILE_PIXELS*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    map_png_t *png=pixels?map_png_create(pixels):NULL;
    int used=0,total=0;bool ok=png!=NULL,header=false;int64_t start=esp_timer_get_time();
    while(ok&&!stale(view->serial)&&esp_timer_get_time()-start<12000000){
        int n=esp_http_client_read(client,(char*)buffer+used,DOWNLOAD_LIMIT-used);
        if(n<0){ok=false;break;}if(!n)break;used+=n;total+=n;
        if(total>PNG_DOWNLOAD_LIMIT){ok=false;break;}
        if(!header){
            if(used<33)continue;
            static const uint8_t signature[]={137,80,78,71,13,10,26,10};
            /* Bound dimensions BEFORE the decoder allocates scanlines. */
            if(memcmp(buffer,signature,8)||memcmp(buffer+12,"IHDR",4)||memcmp(buffer+16,"\0\0\1\365\0\0\3\352",8)){ok=false;break;}
            header=true;
        }
        int consumed=0;
        while(consumed<used&&!stale(view->serial)){
            int nfeed=used-consumed;if(nfeed>1024)nfeed=1024;
            int fed=map_png_feed(png,buffer+consumed,nfeed);
            if(fed<0){ok=false;break;}if(!fed){
                fed=map_png_feed(png,buffer+consumed,used-consumed);
                if(fed<0)ok=false;
                if(fed<=0)break;
            }
            if(fed>used-consumed){ok=false;break;}
            consumed+=fed;
        }
        if(!ok||consumed>used)break;
        used-=consumed;if(used>0)memmove(buffer,buffer+consumed,(size_t)used);
        if(used==DOWNLOAD_LIMIT){ok=false;break;}
    }
    ok=ok&&header&&!stale(view->serial)&&esp_http_client_is_complete_data_received(client)&&map_png_done(png);
    esp_http_client_close(client);map_png_destroy(png);
    if(!ok){free(pixels);return -1;}
    free(tile->pixels);*tile=(map_tile_t){.world=view->world,.lod=lod,.x=tx,.z=tz,.at=now,.pixels=pixels};return 1;
}
static void render_map(map_result_t *out){
    const map_view_t *v=&out->view;int lod=v->zoom/2+1;float span=lod==1?500:lod==2?2500:12500,bpp=blocks_per_pixel[v->zoom];
    for(int y=0;y<360&&!stale(v->serial);y++)for(int x=0;x<360;x++){
        float wx=v->x+(x-180)*bpp,wz=v->z+(y-180)*bpp;int tx=(int)floorf(wx/span),tz=(int)floorf(wz/span);uint16_t color=0x1082;
        for(int i=0;i<9;i++)if(tiles[i].pixels&&tiles[i].world==v->world&&tiles[i].lod==lod&&tiles[i].x==tx&&tiles[i].z==tz){
            int px=(int)((wx-tx*span)/span*TILE_PIXELS),pz=(int)((wz-tz*span)/span*TILE_PIXELS);
            if(px>=0&&px<TILE_PIXELS&&pz>=0&&pz<TILE_PIXELS)color=tiles[i].pixels[pz*TILE_PIXELS+px];
            break;
        }
        out->pixels[y*360+x]=color;
    }
}
static void players(esp_http_client_handle_t client,map_result_t *out,uint8_t *buffer){
    char path[96];snprintf(path,sizeof(path),"/maps/%s/live/players.json",worlds[out->view.world]);
    int n=download(client,path,buffer,16383,out->view.serial);if(n<=0){out->players=-1;return;}buffer[n]=0;
    cJSON *root=cJSON_Parse((char*)buffer),*list=cJSON_GetObjectItem(root,"players"),*p;
    out->players=cJSON_IsArray(list)?cJSON_GetArraySize(list):-1;
    cJSON_ArrayForEach(p,list){
        cJSON *pos=cJSON_GetObjectItem(p,"position"),*x=cJSON_GetObjectItem(pos,"x"),*z=cJSON_GetObjectItem(pos,"z"),*name=cJSON_GetObjectItem(p,"name");
        if(out->count>=12)break;
        if(!cJSON_IsNumber(x)||!cJSON_IsNumber(z)||!isfinite(x->valuedouble)||!isfinite(z->valuedouble)||fabs(x->valuedouble)>30000000||fabs(z->valuedouble)>30000000)continue;
        map_player_t *dest=&out->player[out->count++];dest->x=x->valuedouble;dest->z=z->valuedouble;
        snprintf(dest->name,sizeof(dest->name),"%s",cJSON_IsString(name)?name->valuestring:"");
    }
    cJSON_Delete(root);
}
static void fetch_map(map_result_t *out){
    map_view_t *v=&out->view;char ip[24];wifi_wallpaper_address(ip,sizeof(ip));
    if(!*ip){out->error=1;return;}
    if(time(NULL)<1700000000){
        static bool sntp_started;if(!sntp_started){esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");sntp_started=esp_netif_sntp_init(&config)==ESP_OK;}
        if(!sntp_started||esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000))!=ESP_OK){out->error=2;return;}
    }
    uint8_t *buffer=heap_caps_malloc(DOWNLOAD_LIMIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer){out->error=5;return;}
    esp_http_client_config_t cfg={.url=MAP_ORIGIN,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=5000,.buffer_size=2048,.buffer_size_tx=1024,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client){free(buffer);free(out->pixels);out->pixels=NULL;out->error=5;return;}
    int lod=v->zoom/2+1;float span=lod==1?500:lod==2?2500:12500,bpp=blocks_per_pixel[v->zoom];
    int left=(int)floorf((v->x-180*bpp)/span),right=(int)floorf((v->x+179*bpp)/span);
    int top=(int)floorf((v->z-180*bpp)/span),bottom=(int)floorf((v->z+179*bpp)/span);
    int slot=0;
    for(int z=top;z<=bottom&&!stale(v->serial);z++)for(int x=left;x<=right&&!stale(v->serial);x++){
        /* At least 250 display pixels per tile: at most 3x3 visible tiles. */
        if(slot>=9){out->error=3;break;}
        int ok=get_tile(client,v,lod,x,z,buffer,slot++);if(ok>0)out->tiles++;else if(ok<0)out->error=3;
    }
    if(!stale(v->serial))players(client,out,buffer);
    esp_http_client_cleanup(client);free(buffer);
    /* Allocate the replacement only after HTTP/TLS/PNG workspaces are gone. */
    if(!stale(v->serial)){
        out->pixels=heap_caps_malloc(360*360*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!out->pixels){out->error=5;return;}
        render_map(out);
    }
}
static void run(void *unused){
    (void)unused;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        game_save_t save;
        if(xQueueReceive(saves,&save,0)==pdTRUE){nvs_handle_t h;if(nvs_open("badge_apps",NVS_READWRITE,&h)==ESP_OK){nvs_set_u64(h,"dex",save.dex);nvs_set_i32(h,"best",save.best);nvs_commit(h);nvs_close(h);}}
        map_view_t view;
        if(xQueueReceive(requests,&view,0)==pdTRUE){
            if(view.world<0){release_tiles();atomic_store(&map_busy,false);continue;}
            map_result_t result={.view=view,.players=-1};fetch_map(&result);
            if(stale(view.serial)){free(result.pixels);release_tiles();atomic_store(&map_busy,false);continue;}
            map_result_t old;if(xQueueReceive(results,&old,0)==pdTRUE)free(old.pixels);
            if(xQueueSend(results,&result,0)!=pdTRUE)free(result.pixels);
            atomic_store(&map_busy,false);
        }
    }
}
static bool ensure_worker(void){return requests&&results&&saves&&(worker||xTaskCreate(run,"badge_apps",8192,NULL,2,&worker)==pdPASS);}
static bool map_request(const map_view_t *view){
    if(view->world>=3||view->zoom<0||view->zoom>5||!isfinite(view->x)||!isfinite(view->z)||fabsf(view->x)>30000000||fabsf(view->z)>30000000)return false;
    if(!requests||!results||!saves||!ensure_worker())return false;
    atomic_store(&wanted,view->serial);pending_request=view->world>=0;pending_view=*view;
    if(view->world<0){
        atomic_store(&map_busy,true);
        map_result_t old;while(xQueueReceive(results,&old,0)==pdTRUE)free(old.pixels);
        xQueueOverwrite(requests,view);xTaskNotifyGive(worker);
    }
    return true;
}
static void save_game(uint64_t dex,int best){
    if(!saves||!ensure_worker())return;
    game_save_t save={dex,best};xQueueOverwrite(saves,&save);xTaskNotifyGive(worker);
}
void app_service_init(void){
    requests=xQueueCreate(1,sizeof(map_view_t));results=xQueueCreate(1,sizeof(map_result_t));saves=xQueueCreate(1,sizeof(game_save_t));
    uint64_t dex=0;int32_t best=0;nvs_handle_t h;
    if(nvs_open("badge_apps",NVS_READONLY,&h)==ESP_OK){nvs_get_u64(h,"dex",&dex);nvs_get_i32(h,"best",&best);nvs_close(h);}
    badge_apps_saved(dex,best);badge_apps_bind(map_request,save_game);
}
bool app_service_map_busy(void){return atomic_load(&map_busy);}
void app_service_poll(void){
    map_result_t result;if(results&&xQueueReceive(results,&result,0)==pdTRUE)badge_apps_map_result(&result);
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1536*1024)ui_memory_pressure_set(UI_MEMORY_CRITICAL);
    if(pending_request&&!app_service_map_busy()&&!badge_ui_transition_active()&&!physical_display_transition_active()){
        /* The GUI remains responsive while waiting for actual display ownership and RAM. */
        if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<40*1024||heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)<32*1024)return;
        physical_display_wait();atomic_store(&map_busy,true);pending_request=false;
        xQueueOverwrite(requests,&pending_view);xTaskNotifyGive(worker);
    }
    int page,loading,error,tiles_count,players_count;badge_apps_status(&page,&loading,&error,&tiles_count,&players_count);
    if((page<1||page==2||page==4)&&!app_service_map_busy()&&!pending_request&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>2*1024*1024&&ui_memory_pressure_get()!=UI_MEMORY_NORMAL)ui_memory_pressure_set(UI_MEMORY_NORMAL);
}
