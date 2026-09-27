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
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "map_png.h"
#include "map_perf.h"
#include "map_cache.h"
#include "map_lane_policy.h"
#include "ui/ui_transition_cache.h"
#include "physical_display.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#define MAP_ORIGIN "https://bluemap.orangedog.nbtca.space:8001"
#define TILE_PIXELS 128
#define DOWNLOAD_LIMIT 16384
#define PNG_DOWNLOAD_LIMIT (1024*1024)
#define TILE_TIMEOUT_US 30000000
/* LodePNG's supported custom allocator hook: PNG scratch must not consume DMA RAM. */
void *lodepng_malloc(size_t n){return n<=2100000?heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;}
void *lodepng_realloc(void *p,size_t n){return n<=2100000?heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;}
void lodepng_free(void *p){free(p);}
typedef struct {uint64_t dex;int best;} game_save_t;
typedef struct {map_view_t view;int64_t requested;} map_job_t;
typedef struct {map_result_t map;map_perf_t perf;} map_reply_t;
static QueueHandle_t requests,results,saves;
static TaskHandle_t worker;
static atomic_uint wanted;
static atomic_int wanted_world,wanted_lod,wanted_x,wanted_z;
static atomic_bool map_busy;
static bool pending_request;
static map_job_t pending_job;
static map_cache_t cache;
typedef struct {map_view_t view;map_key_t key;int kind;} tile_job_t; /* 0 tile, 1 players, 2 stop, 3 background tile */
typedef struct {int lane,kind;map_key_t key;map_perf_t perf;map_result_t map;} tile_done_t;
static struct {QueueHandle_t jobs;TaskHandle_t task;esp_http_client_handle_t client;uint8_t *buffer;} lanes[2];
static atomic_bool shed_secondary;
static void pump_secondary(void);
static QueueHandle_t tile_done;
static struct {map_reply_t patch;tile_done_t primary,secondary;} *scratch;
static map_key_t background_queue[9],background_key;
static map_view_t background_view;
static int background_count,background_next;
static int64_t background_expected;
static bool background_active;
static const char *worlds[]={"world","world_the_nether","world_the_end"};
static const float blocks_per_pixel[]={1,2,5,10,25,50};
static bool stale(uint32_t serial){return serial!=atomic_load(&wanted);}
static bool useful_tile(const map_perf_t *p){
    if(!p->lod||p->world!=atomic_load(&wanted_world)||p->lod!=atomic_load(&wanted_lod))return false;
    int dx=p->x-atomic_load(&wanted_x),dz=p->z-atomic_load(&wanted_z);
    return abs(dx)<=2&&abs(dz)<=2&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>1024*1024;
}
/* Time only HTTP API residence, excluding interleaved PNG feed work.
 * Header event is a parsed-first-header TTFB approximation, not wire timing. */
#define MAP_HTTP(p,call) ({int64_t t_=esp_timer_get_time();__auto_type value_=(call);(p)->network_us+=esp_timer_get_time()-t_;value_;})
static int tile_error(map_perf_t *p,int code,const char *category,const char *detail){
    p->error_category=category;p->error_detail=detail;return code;
}
static int png_error(map_perf_t *p,const map_png_t *png){
    const char *detail=map_png_error(png);
    if(!detail)detail="PNG parser rejected input";
    if(strstr(detail,"dimensions"))return tile_error(p,MAP_TILE_SIZE,"SIZE",detail);
    if(strstr(detail,"CRC"))return tile_error(p,MAP_TILE_CRC,"CRC",detail);
    if(strstr(detail,"decompress")||strstr(detail,"filter")||strstr(detail,"Color index"))
        return tile_error(p,MAP_TILE_DECODE,"DECODE",detail);
    if(strstr(detail,"memory"))return tile_error(p,MAP_TILE_OTHER,"OTHER",detail);
    return tile_error(p,MAP_TILE_PNG_PARSE,"PNG_PARSE",detail);
}
static esp_err_t http_event(esp_http_client_event_t *event){
    map_perf_t *p=event->user_data;
    if(p&&(event->event_id==HTTP_EVENT_ON_HEADER||event->event_id==HTTP_EVENT_ON_DATA)&&!p->first_byte)p->first_byte=esp_timer_get_time();
    if(p&&event->event_id==HTTP_EVENT_ON_CONNECTED)p->connects++;
    if(p&&event->event_id==HTTP_EVENT_ON_DATA&&event->data_len>0)p->bytes+=event->data_len;
    return ESP_OK;
}
static bool cancelled(uint32_t serial,const map_perf_t *p){
    if(p->lane==1&&atomic_load(&shed_secondary))return true;
    return stale(serial)&&!useful_tile(p);
}
/* One network reader per active tile. Two PSRAM slots let the next HTTP read
 * proceed while the owning lane feeds Pngle from its existing 16 KiB buffer. */
typedef struct {int slot,n;} map_chunk_t;
typedef struct {
    QueueHandle_t free_slots,ready_slots;
    TaskHandle_t reader;
    esp_http_client_handle_t client;
    map_perf_t *perf;
    uint32_t serial;
    atomic_bool abort;
    int read_errno;
    map_chunk_t held;
    int held_offset;
    uint8_t slot[2][8192];
} map_pipe_t;
static void map_reader(void *arg){
    map_pipe_t *pipe=arg;
    int outcome=0;
    for(;;){
        if(atomic_load(&pipe->abort)||cancelled(pipe->serial,pipe->perf))break;
        int slot;
        xQueueReceive(pipe->free_slots,&slot,portMAX_DELAY);
        if(atomic_load(&pipe->abort)||cancelled(pipe->serial,pipe->perf))break;
        int n=MAP_HTTP(pipe->perf,esp_http_client_read(pipe->client,(char*)pipe->slot[slot],8192));
        if(esp_http_client_is_complete_data_received(pipe->client)&&!pipe->perf->download_done)pipe->perf->download_done=esp_timer_get_time();
        if(n<=0){outcome=n;pipe->read_errno=esp_http_client_get_errno(pipe->client);break;}
        map_chunk_t chunk={slot,n};xQueueSend(pipe->ready_slots,&chunk,portMAX_DELAY);
    }
    map_chunk_t end={-1,outcome};xQueueSend(pipe->ready_slots,&end,portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY); /* Owner deletes the parked reader before freeing the ring. */
}
static map_pipe_t *map_pipe_start(esp_http_client_handle_t client,uint32_t serial,map_perf_t *perf){
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1536*1024)return NULL;
    map_pipe_t *pipe=heap_caps_calloc(1,sizeof(*pipe),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pipe)return NULL;
    pipe->client=client;pipe->perf=perf;pipe->serial=serial;
    pipe->held.slot=-1;
    pipe->free_slots=xQueueCreate(2,sizeof(int));pipe->ready_slots=xQueueCreate(3,sizeof(map_chunk_t));
    if(pipe->free_slots&&pipe->ready_slots){
        for(int i=0;i<2;i++)xQueueSend(pipe->free_slots,&i,0);
        if(xTaskCreateWithCaps(map_reader,"map_read",6144,pipe,2,&pipe->reader,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return pipe;
    }
    if(pipe->free_slots)vQueueDelete(pipe->free_slots);
    if(pipe->ready_slots)vQueueDelete(pipe->ready_slots);
    free(pipe);return NULL;
}
static int map_pipe_next(map_pipe_t *pipe,uint8_t *dst,size_t capacity){
    if(pipe->held.slot<0){
        xQueueReceive(pipe->ready_slots,&pipe->held,portMAX_DELAY);
        if(pipe->held.slot<0){pipe->perf->http_errno=pipe->read_errno;return pipe->held.n;}
        pipe->held_offset=0;
    }
    int n=pipe->held.n-pipe->held_offset;
    if(n>(int)capacity)n=(int)capacity;
    memcpy(dst,pipe->slot[pipe->held.slot]+pipe->held_offset,n);
    pipe->held_offset+=n;
    if(pipe->held_offset==pipe->held.n){xQueueSend(pipe->free_slots,&pipe->held.slot,portMAX_DELAY);pipe->held.slot=-1;}
    return n;
}
static void map_pipe_finish(map_pipe_t *pipe,bool ended){
    if(!pipe)return;
    atomic_store(&pipe->abort,true);
    if(pipe->held.slot>=0)xQueueSend(pipe->free_slots,&pipe->held.slot,portMAX_DELAY);
    while(!ended){map_chunk_t chunk;xQueueReceive(pipe->ready_slots,&chunk,portMAX_DELAY);
        if(chunk.slot<0)ended=true;
        else xQueueSend(pipe->free_slots,&chunk.slot,portMAX_DELAY);
    }
    while(eTaskGetState(pipe->reader)!=eBlocked)vTaskDelay(1);
    vTaskDeleteWithCaps(pipe->reader);
    vQueueDelete(pipe->free_slots);vQueueDelete(pipe->ready_slots);free(pipe);
}
static int png_feed(map_png_t *png,const uint8_t *data,size_t bytes,map_perf_t *p){
    int64_t start=esp_timer_get_time();int n=map_png_feed(png,data,bytes);
    int64_t end=esp_timer_get_time();p->decode_us+=end-start;
    if(map_png_done(png)&&!p->png_done)p->png_done=end;
    if(p->lane==0)pump_secondary();
    return n;
}
/* Keep a completely consumed HTTP/1.1 response on its socket. TCP keepalive
 * alone does not provide HTTP reuse. Broken idle sockets get one reconnect. */
static int request_begin(esp_http_client_handle_t client,const char *url,uint32_t serial,map_perf_t *p){
    p->request=esp_timer_get_time();esp_http_client_set_user_data(client,p);
    bool warm=esp_http_client_get_socket(client)>=0;
    int fail=MAP_TILE_HTTP;
    for(int attempt=0;attempt<2&&!cancelled(serial,p);attempt++){
        unsigned connected=p->connects;
        esp_err_t err=MAP_HTTP(p,esp_http_client_set_url(client,url));
        if(err!=ESP_OK)fail=tile_error(p,MAP_TILE_HTTP,"HTTP","set URL failed");
        else if((err=MAP_HTTP(p,esp_http_client_open(client,0)))!=ESP_OK){
            p->http_errno=esp_http_client_get_errno(client);
            fail=tile_error(p,p->http_errno==ETIMEDOUT?MAP_TILE_TIMEOUT:MAP_TILE_CONNECTION,
                p->http_errno==ETIMEDOUT?"TIMEOUT":"CONNECTION","open or TLS failed");
        }else{
            int64_t length=MAP_HTTP(p,esp_http_client_fetch_headers(client));
            if(p->lane==0)pump_secondary();
            if(length>=0){p->status=esp_http_client_get_status_code(client);p->reused=warm&&p->connects==connected;
                p->error_category=NULL;p->error_detail=NULL;p->http_errno=0;return 1;}
            p->http_errno=esp_http_client_get_errno(client);
            fail=tile_error(p,p->http_errno==ETIMEDOUT||length==-ESP_ERR_HTTP_EAGAIN?MAP_TILE_TIMEOUT:MAP_TILE_HTTP,
                p->http_errno==ETIMEDOUT||length==-ESP_ERR_HTTP_EAGAIN?"TIMEOUT":"HTTP","fetch headers failed");
        }
        MAP_HTTP(p,esp_http_client_close(client));
        if(!warm)break;
        warm=false;p->first_byte=0;
    }
    return cancelled(serial,p)?-2:fail;
}
static void request_end(esp_http_client_handle_t client,bool complete,map_perf_t *p){
    p->persistent=complete&&esp_http_client_is_persistent_connection(client);
    if(!p->persistent)MAP_HTTP(p,esp_http_client_close(client));
}
static int download(esp_http_client_handle_t client,const char *path,uint8_t *buffer,int cap,uint32_t serial,map_perf_t *perf){
    char url[256];snprintf(url,sizeof(url),MAP_ORIGIN "%s",path);
    if(request_begin(client,url,serial,perf)!=1)return -1;
    int used=0;
    if(perf->status!=200||esp_http_client_get_content_length(client)>cap){request_end(client,false,perf);return -1;}
    int64_t start=esp_timer_get_time();
    while(used<cap&&!cancelled(serial,perf)&&esp_timer_get_time()-start<12000000){
        int n=MAP_HTTP(perf,esp_http_client_read(client,(char*)buffer+used,cap-used));
        if(esp_http_client_is_complete_data_received(client)&&!perf->download_done)perf->download_done=esp_timer_get_time();
        if(n<0){used=-1;break;}if(!n)break;used+=n;
    }
    bool complete=used>=0&&esp_http_client_is_complete_data_received(client);
    request_end(client,complete,perf);return !stale(serial)&&complete?used:-1;
}
static void coordinate_path(char *out,int n){
    char digits[16];snprintf(digits,sizeof(digits),"%d",n);char *p=out;
    for(char *s=digits;*s;s++){*p++=*s;if(*s!='-')*p++='/';}*p=0;
}
static int read_error(map_perf_t *p,int n){
    if(n==-ESP_ERR_HTTP_EAGAIN||p->http_errno==ETIMEDOUT)
        return tile_error(p,MAP_TILE_TIMEOUT,"TIMEOUT","HTTP read timed out");
    if(p->http_errno==ECONNRESET||p->http_errno==ENOTCONN)
        return tile_error(p,MAP_TILE_CONNECTION,"CONNECTION","connection lost during body");
    return tile_error(p,MAP_TILE_READ,"READ",n<0?"HTTP read failed":"incomplete HTTP body");
}
static int get_tile(esp_http_client_handle_t client,const map_view_t *view,int lod,int tx,int tz,uint8_t *buffer,uint16_t **decoded,map_perf_t *perf){
    char x[32],z[32],path[160];coordinate_path(x,tx);coordinate_path(z,tz);z[strlen(z)-1]=0;
    snprintf(path,sizeof(path),"/maps/%s/tiles/%d/x%sz%s.png",worlds[view->world],lod,x,z);
    if(cancelled(view->serial,perf))return -2;
    char url[256];snprintf(url,sizeof(url),MAP_ORIGIN "%s",path);
    int begin=request_begin(client,url,view->serial,perf);if(begin!=1)return begin;
    int status=perf->status;
    if(status!=200||esp_http_client_get_content_length(client)>PNG_DOWNLOAD_LIMIT){
        request_end(client,false,perf);
        if(status==404)return 0;
        return status!=200?tile_error(perf,MAP_TILE_HTTP,"HTTP","unexpected status"):
            tile_error(perf,MAP_TILE_SIZE,"SIZE","HTTP body exceeds 1 MiB");
    }
    uint16_t *pixels=heap_caps_malloc(TILE_PIXELS*TILE_PIXELS*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    map_png_t *png=pixels?map_png_create(pixels):NULL;
    if(!png){request_end(client,false,perf);free(pixels);return tile_error(perf,-4,"OTHER","PNG allocation failed");}
    map_pipe_t *pipe=png?map_pipe_start(client,view->serial,perf):NULL;
    int used=0,total=0,error=0;bool header=false,ended=false;int64_t start=esp_timer_get_time();
    while(!error&&!cancelled(view->serial,perf)&&esp_timer_get_time()-start<TILE_TIMEOUT_US){
        int n=pipe?map_pipe_next(pipe,buffer+used,DOWNLOAD_LIMIT-used):
            MAP_HTTP(perf,esp_http_client_read(client,(char*)buffer+used,DOWNLOAD_LIMIT-used));
        if(!pipe&&n<=0)perf->http_errno=esp_http_client_get_errno(client);
        if(!pipe&&esp_http_client_is_complete_data_received(client)&&!perf->download_done)perf->download_done=esp_timer_get_time();
        if(perf->lane==0)pump_secondary();
        if(n<0){ended=pipe!=NULL;error=read_error(perf,n);break;}if(!n){ended=true;break;}used+=n;total+=n;
        if(total>PNG_DOWNLOAD_LIMIT){error=tile_error(perf,MAP_TILE_SIZE,"SIZE","PNG exceeds 1 MiB");break;}
        if(!header){
            if(used<33)continue;
            static const uint8_t signature[]={137,80,78,71,13,10,26,10};
            /* Bound dimensions BEFORE the decoder allocates scanlines. */
            if(memcmp(buffer,signature,8)||memcmp(buffer+12,"IHDR",4)){
                error=tile_error(perf,MAP_TILE_PNG_PARSE,"PNG_PARSE","invalid PNG signature or IHDR");break;
            }
            if(memcmp(buffer+16,"\0\0\1\365\0\0\3\352",8)){
                error=tile_error(perf,MAP_TILE_SIZE,"SIZE","unexpected image dimensions");break;
            }
            header=true;
        }
        int consumed=0;
        while(consumed<used&&!cancelled(view->serial,perf)){
            int nfeed=used-consumed;if(nfeed>1024)nfeed=1024;
            int fed=png_feed(png,buffer+consumed,nfeed,perf);
            if(fed<0){error=png_error(perf,png);break;}if(!fed){
                fed=png_feed(png,buffer+consumed,used-consumed,perf);
                if(fed<0)error=png_error(perf,png);
                if(fed<=0)break;
            }
            if(fed>used-consumed){error=tile_error(perf,MAP_TILE_OTHER,"OTHER","PNG consumed beyond input");break;}
            consumed+=fed;
        }
        if(error)break;
        used-=consumed;if(used>0)memmove(buffer,buffer+consumed,(size_t)used);
        if(used==DOWNLOAD_LIMIT){error=tile_error(perf,MAP_TILE_PNG_PARSE,"PNG_PARSE","PNG parser stalled");break;}
    }
    map_pipe_finish(pipe,ended);
    bool complete=esp_http_client_is_complete_data_received(client);
    if(!error&&cancelled(view->serial,perf))error=-2;
    if(!error&&!complete&&esp_timer_get_time()-start>=TILE_TIMEOUT_US)
        error=tile_error(perf,MAP_TILE_TIMEOUT,"TIMEOUT","Tile exceeded 30 seconds");
    if(!error&&!complete)error=read_error(perf,0);
    if(!error&&(!header||!map_png_done(png)))
        error=tile_error(perf,MAP_TILE_PNG_PARSE,"PNG_PARSE","complete HTTP body lacks PNG end");
    request_end(client,!error&&complete,perf);map_png_destroy(png);
    if(error){free(pixels);return error;}
    *decoded=pixels;return 1;
}
static void players(esp_http_client_handle_t client,map_result_t *out,uint8_t *buffer,map_perf_t *perf){
    char path[96];snprintf(path,sizeof(path),"/maps/%s/live/players.json",worlds[out->view.world]);
    int n=download(client,path,buffer,16383,out->view.serial,perf);if(n<=0){out->players=-1;return;}buffer[n]=0;
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
static void close_lane(int lane){
    if(lanes[lane].client)esp_http_client_cleanup(lanes[lane].client);
    free(lanes[lane].buffer);lanes[lane].client=NULL;lanes[lane].buffer=NULL;
}
static void perform_job(int lane,const tile_job_t *job,tile_done_t *done){
    *done=(tile_done_t){.lane=lane,.kind=job->kind,.key=job->key,.map={.view=job->view,.players=-1},
        .perf={.serial=job->view.serial,.world=job->view.world,.zoom=job->view.zoom,.lod=job->key.lod,.x=job->key.x,.z=job->key.z,.lane=lane,.background=job->kind==3,.start=esp_timer_get_time()}};
    if(!cancelled(job->view.serial,&done->perf)){
        if(!lanes[lane].buffer)lanes[lane].buffer=heap_caps_malloc(DOWNLOAD_LIMIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!lanes[lane].client&&heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=40*1024&&
            heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)>=32*1024){
            esp_http_client_config_t cfg={.url=MAP_ORIGIN,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=5000,
                .buffer_size=8192,.buffer_size_tx=1024,.disable_auto_redirect=true,.event_handler=http_event,.keep_alive_enable=true};
            lanes[lane].client=esp_http_client_init(&cfg);
            if(lanes[lane].client)esp_http_client_set_header(lanes[lane].client,"Connection","keep-alive");
        }
        if(lanes[lane].client&&lanes[lane].buffer){
            if(job->kind==0||job->kind==3)done->perf.result=get_tile(lanes[lane].client,&job->view,job->key.lod,job->key.x,job->key.z,lanes[lane].buffer,&done->map.pixels,&done->perf);
            else {players(lanes[lane].client,&done->map,lanes[lane].buffer,&done->perf);done->perf.result=done->map.players>=0?1:-1;}
            esp_http_client_set_user_data(lanes[lane].client,NULL);
        }else done->perf.result=tile_error(&done->perf,-4,"OTHER","HTTP lane allocation failed");
    }else done->perf.result=tile_error(&done->perf,-2,"CANCELLED","request superseded");
    if(done->perf.result<0&&stale(job->view.serial))done->perf.result=tile_error(&done->perf,-2,"CANCELLED","request superseded");
    else if(done->perf.result<0&&lane==1&&atomic_load(&shed_secondary))
        done->perf.result=tile_error(&done->perf,-3,"MEMORY","secondary lane shed");
    done->perf.end=esp_timer_get_time();
    if(job->kind==0||job->kind==3)map_perf_publish(&done->perf);
}
static void tile_worker(void *unused){
    (void)unused;
    for(;;){
        tile_job_t job;xQueueReceive(lanes[1].jobs,&job,portMAX_DELAY);tile_done_t done;
        if(job.kind==2){close_lane(1);done=(tile_done_t){.kind=2};xQueueSend(tile_done,&done,portMAX_DELAY);vTaskDelete(NULL);}
        perform_job(1,&job,&done);xQueueSend(tile_done,&done,portMAX_DELAY);
        if(job.kind==3)xTaskNotifyGive(worker);
    }
}
static bool collect_background(TickType_t wait,bool keep){
    if(!background_active)return true;
    tile_done_t done;
    if(xQueueReceive(tile_done,&done,wait)!=pdTRUE)return false;
    background_active=false;
    if(done.map.pixels){
        map_cached_tile_t *old=keep?map_cache_find(&cache,background_key,esp_timer_get_time()):NULL;
        if(!old||old->updated!=background_expected||!map_cache_put(&cache,background_key,done.map.pixels,esp_timer_get_time(),old->pinned))
            free(done.map.pixels);
    }
    return true;
}
static void stop_secondary(void){
    if(!lanes[1].task)return;
    if(background_active){atomic_store(&shed_secondary,true);collect_background(portMAX_DELAY,false);}
    tile_job_t stop={.kind=2};
    xQueueSend(lanes[1].jobs,&stop,portMAX_DELAY);xQueueReceive(tile_done,&scratch->secondary,portMAX_DELAY);lanes[1].task=NULL;
}
static const char *lane_memory(bool new_task,bool new_client,map_perf_t *perf){
    size_t internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    size_t dma=heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    size_t psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(perf){perf->internal_free=internal;perf->dma_free=dma;perf->psram_free=psram;}
    return map_lane_memory_reason(internal,dma,psram,
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),new_task,new_client);
}
static void start_secondary(map_perf_t *perf){
    if(!background_active)atomic_store(&shed_secondary,false);
    perf->requested_lanes=2;perf->actual_lanes=1;
    perf->fallback_reason=lane_memory(!lanes[1].task,!lanes[1].client,perf);
    if(perf->fallback_reason){
        if(background_active)atomic_store(&shed_secondary,true);
        else stop_secondary();
        return;
    }
    if(background_active){perf->fallback_reason="BACKGROUND_BUSY";return;}
    if(!lanes[1].task){
        if(!tile_done)tile_done=xQueueCreate(1,sizeof(tile_done_t));
        if(!lanes[1].jobs)lanes[1].jobs=xQueueCreate(1,sizeof(tile_job_t));
        if(!tile_done||!lanes[1].jobs){perf->fallback_reason="QUEUE_ALLOC_FAILED";return;}
        if(xTaskCreate(tile_worker,"map_http1",8192,NULL,2,&lanes[1].task)!=pdPASS){perf->fallback_reason="TASK_ALLOC_FAILED";return;}
    }
    perf->actual_lanes=2;
}
static void discard_reply(map_reply_t *reply){
    free(reply->map.pixels);
    if(reply->perf.refresh){reply->perf.result=-2;reply->perf.end=esp_timer_get_time();map_perf_publish(&reply->perf);}
}
static bool send_reply(map_reply_t *reply){
    while(!stale(reply->map.view.serial))if(xQueueSend(results,reply,pdMS_TO_TICKS(20))==pdTRUE)return true;
    discard_reply(reply);return false;
}
static void publish_tile(const map_view_t *v,map_cached_tile_t *tile,int count,map_perf_t *perf){
    if(stale(v->serial))return;
    int x,y,w,h;map_tile_bounds(tile->key,v->x,v->z,blocks_per_pixel[v->zoom],&x,&y,&w,&h);
    /* No extra full map: at most 16 rows per queued patch (11,520 bytes). */
    for(int row=0;row<h;row+=16){
        scratch->patch=(map_reply_t){.map={.view=*v,.partial=true,.players=-1,.tiles=count,.x=x,.y=y+row,.w=w,.h=h-row<16?h-row:16}};
        map_result_t *r=&scratch->patch.map;
        if(stale(v->serial)||!w)return;
        r->pixels=heap_caps_malloc(r->w*r->h*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!r->pixels){perf->patch_failed=true;return;}
        int64_t start=esp_timer_get_time();
        map_tile_sample(tile,v->x,v->z,blocks_per_pixel[v->zoom],r->x,r->y,r->w,r->h,r->pixels,r->w);
        perf->compose_us+=esp_timer_get_time()-start;
        if(!send_reply(&scratch->patch))return;
    }
    perf->updates++;
}
/* The existing badge_apps worker is lane 0 and sole cache owner. While it
 * streams/decodes, these cooperative checkpoints drain lane 1 completions. */
static struct {map_result_t *out;map_perf_t *perf;bool secondary_busy,retry,primary_busy;tile_job_t retry_job,*jobs;int next,total;} fetching;
static void accept_tile(tile_done_t *done){
    map_result_t *out=fetching.out;map_perf_t *p=fetching.perf;map_view_t *v=&out->view;
    p->network_us+=done->perf.network_us;p->decode_us+=done->perf.decode_us;p->bytes+=done->perf.bytes;
    p->connects+=done->perf.connects;p->reuse_count+=done->perf.reused;
    if(done->map.pixels){
        map_cached_tile_t *t=map_cache_put(&cache,done->key,done->map.pixels,esp_timer_get_time(),!stale(v->serial));
        if(t){out->tiles++;p->downloaded++;publish_tile(v,t,out->tiles,p);}
        else {free(done->map.pixels);out->error=5;}
        while(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1024*1024&&map_cache_evict_cold(&cache)){}
    }else if(done->lane==1&&(done->perf.result==-3||done->perf.result==-4)&&!stale(v->serial)){
        fetching.retry=true;fetching.retry_job=(tile_job_t){.view=*v,.key=done->key};atomic_store(&shed_secondary,true);
        p->actual_lanes=1;
        if(!p->fallback_reason)p->fallback_reason=lane_memory(false,false,p);
        if(!p->fallback_reason)p->fallback_reason=done->perf.result==-4?"HTTP_ALLOC_FAILED":"RUNTIME_LOW_WATER";
    }else if(done->perf.result<0)out->error=done->perf.result==-4?5:3;
}
static void schedule_secondary(void){
    if(lanes[1].task&&!background_active&&!fetching.secondary_busy&&fetching.next<fetching.total&&!stale(fetching.out->view.serial)&&!atomic_load(&shed_secondary)){
        xQueueSend(lanes[1].jobs,&fetching.jobs[fetching.next++],portMAX_DELAY);fetching.secondary_busy=true;
        fetching.perf->actual_lanes=2;
        if(fetching.perf->fallback_reason&&!strcmp(fetching.perf->fallback_reason,"BACKGROUND_BUSY"))fetching.perf->fallback_reason=NULL;
        unsigned active=fetching.primary_busy?2:1;if(active>fetching.perf->max_inflight)fetching.perf->max_inflight=active;
    }
}
static void pump_secondary(void){
    if(background_active&&!collect_background(0,true))return;
    if(atomic_load(&shed_secondary)&&!lane_memory(false,false,NULL))atomic_store(&shed_secondary,false);
    if(!fetching.secondary_busy){schedule_secondary();return;}
    const char *reason=lane_memory(false,false,NULL);
    if(reason){
        if(!fetching.perf->fallback_reason){fetching.perf->fallback_reason=reason;lane_memory(false,false,fetching.perf);}
        fetching.perf->actual_lanes=1;atomic_store(&shed_secondary,true);
    }
    if(xQueueReceive(tile_done,&scratch->secondary,0)==pdTRUE){
        fetching.secondary_busy=false;accept_tile(&scratch->secondary);
        if(atomic_load(&shed_secondary))stop_secondary();
        else schedule_secondary();
    }
}
static bool same_key(map_key_t a,map_key_t b){return a.world==b.world&&a.lod==b.lod&&a.x==b.x&&a.z==b.z;}
static void queue_background(map_key_t key){
    if(background_active&&same_key(background_key,key))return;
    for(int i=background_next;i<background_count;i++)if(same_key(background_queue[i],key))return;
    if(background_count<9)background_queue[background_count++]=key;
}
static void schedule_background(void){
    if(background_active||stale(background_view.serial)||atomic_load(&shed_secondary)||!lanes[1].task||lane_memory(false,false,NULL))return;
    while(background_next<background_count){
        map_key_t key=background_queue[background_next++];int64_t now=esp_timer_get_time();
        map_cached_tile_t *old=map_cache_find(&cache,key,now);
        if(!old||now-old->updated<60000000)continue;
        tile_job_t job={.view=background_view,.key=key,.kind=3};
        if(xQueueSend(lanes[1].jobs,&job,0)!=pdTRUE)return;
        background_key=key;background_expected=old->updated;background_active=true;return;
    }
}
static void fetch_map(map_result_t *out,map_perf_t *perf){
    perf->requested_lanes=2;perf->fallback_reason="NOT_STARTED";lane_memory(false,false,perf);
    map_view_t *v=&out->view;background_count=background_next=0;background_view=*v;
    char ip[24];wifi_wallpaper_address(ip,sizeof(ip));
    if(!*ip){out->error=1;return;}
    if(time(NULL)<1700000000){
        static bool sntp_started;if(!sntp_started){esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");sntp_started=esp_netif_sntp_init(&config)==ESP_OK;}
        if(!sntp_started||esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000))!=ESP_OK){out->error=2;return;}
    }
    if(stale(v->serial))return;
    if(!scratch)scratch=heap_caps_calloc(1,sizeof(*scratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!scratch){out->error=5;return;}
    start_secondary(perf);
    int lod=v->zoom/2+1;float span=lod==1?500:lod==2?2500:12500,bpp=blocks_per_pixel[v->zoom];
    int left=(int)floorf((v->x-180*bpp)/span),right=(int)floorf((v->x+179*bpp)/span);
    int top=(int)floorf((v->z-180*bpp)/span),bottom=(int)floorf((v->z+179*bpp)/span);
    perf->needed=(right-left+1)*(bottom-top+1);
    if(perf->needed>9){out->error=3;return;}
    map_cache_unpin(&cache);
    tile_job_t missing[9];int misses=0;
    scratch->patch=(map_reply_t){.map={.view=*v,.partial=true,.reset=true,.players=-1,.w=360,.h=360}};
    if(!send_reply(&scratch->patch))return;
    for(int z=top;z<=bottom;z++)for(int x=left;x<=right;x++){
        map_key_t key={v->world,lod,x,z};int64_t now=esp_timer_get_time();
        map_cached_tile_t *t=map_cache_find(&cache,key,now);
        if(t){
            t->pinned=true;
        }
        if(t){
            out->tiles++;perf->hits++;
            bool expired=now-t->updated>=60000000;
            if(expired){perf->expired++;queue_background(key);}
            map_perf_t hit={.serial=v->serial,.world=v->world,.zoom=v->zoom,.lod=lod,.x=x,.z=z,.hit=true,.result=1,.start=now,.end=esp_timer_get_time()};
            map_perf_publish(&hit);
        }else {missing[misses++]=(tile_job_t){.view=*v,.key=key};perf->misses++;}
        if(t)publish_tile(v,t,out->tiles,perf);
    }
    fetching.out=out;fetching.perf=perf;fetching.secondary_busy=false;fetching.retry=false;fetching.primary_busy=false;
    fetching.jobs=missing;fetching.next=0;fetching.total=misses;
    if(misses&&background_active)atomic_store(&shed_secondary,true);
    int64_t network_start=esp_timer_get_time();
    while(fetching.next<misses||fetching.retry||fetching.secondary_busy){
        schedule_secondary();
        if(!stale(v->serial)&&(fetching.next<misses||fetching.retry)){
            tile_job_t job=fetching.retry?fetching.retry_job:missing[fetching.next++];fetching.retry=false;
            unsigned active=fetching.secondary_busy?2:1;if(active>perf->max_inflight)perf->max_inflight=active;
            fetching.primary_busy=true;perform_job(0,&job,&scratch->primary);fetching.primary_busy=false;
            pump_secondary();accept_tile(&scratch->primary);
        }else if(fetching.secondary_busy){
            xQueueReceive(tile_done,&scratch->secondary,portMAX_DELAY);fetching.secondary_busy=false;accept_tile(&scratch->secondary);
            if(atomic_load(&shed_secondary))stop_secondary();
        }else break;
    }
    if(!stale(v->serial)){
        tile_job_t player={.kind=1,.view=*v};tile_done_t *done=&scratch->primary;perform_job(0,&player,done);
        out->players=done->map.players;out->count=done->map.count;memcpy(out->player,done->map.player,sizeof(out->player));
        perf->players_us=done->perf.end-done->perf.start;perf->network_us+=done->perf.network_us;perf->bytes+=done->perf.bytes;
        perf->connects+=done->perf.connects;perf->reuse_count+=done->perf.reused;
    }
    if(atomic_load(&shed_secondary)){
        perf->actual_lanes=1;
        if(!perf->fallback_reason){perf->fallback_reason=lane_memory(false,false,perf);if(!perf->fallback_reason)perf->fallback_reason="RUNTIME_LOW_WATER";}
    }
    if(perf->patch_failed)out->error=5;
    perf->network_wall_us=esp_timer_get_time()-network_start;fetching.out=NULL;fetching.perf=NULL;

}
static void run(void *unused){
    (void)unused;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(background_active)collect_background(0,true);
        if(!background_active&&atomic_load(&shed_secondary)&&!lane_memory(false,false,NULL))atomic_store(&shed_secondary,false);
        if(!background_active&&atomic_load(&shed_secondary)&&lane_memory(false,false,NULL))stop_secondary();
        game_save_t save;
        if(xQueueReceive(saves,&save,0)==pdTRUE){nvs_handle_t h;if(nvs_open("badge_apps",NVS_READWRITE,&h)==ESP_OK){nvs_set_u64(h,"dex",save.dex);nvs_set_i32(h,"best",save.best);nvs_commit(h);nvs_close(h);}}
        map_job_t job;
        if(xQueueReceive(requests,&job,0)==pdTRUE){
            map_view_t view=job.view;
            if(view.world<0){
                stop_secondary();close_lane(0);
                background_count=background_next=0;
                /* HTTP/page lifetime is not tile lifetime. Keep the bounded LRU. */
                map_cache_unpin(&cache);
                while(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1536*1024&&map_cache_evict_cold(&cache)){}
                free(scratch);scratch=NULL;atomic_store(&map_busy,false);continue;
            }
            map_reply_t reply={.map={.view=view,.players=-1},.perf={.refresh=true,.serial=view.serial,.world=view.world,.zoom=view.zoom,
                .center_x=view.x,.center_z=view.z,.start=job.requested,.request=esp_timer_get_time()}};
            fetch_map(&reply.map,&reply.perf);reply.perf.worker_done=esp_timer_get_time();reply.perf.result=reply.map.error;
            send_reply(&reply);if(atomic_load(&shed_secondary)&&!background_active)stop_secondary();atomic_store(&map_busy,false);
        }
        schedule_background();
    }
}
static bool ensure_worker(void){return requests&&results&&saves&&(worker||xTaskCreate(run,"badge_apps",8192,NULL,2,&worker)==pdPASS);}
static bool map_request(const map_view_t *view){
    if(view->world>=3||view->zoom<0||view->zoom>5||!isfinite(view->x)||!isfinite(view->z)||fabsf(view->x)>30000000||fabsf(view->z)>30000000)return false;
    if(!requests||!results||!saves||!ensure_worker())return false;
    int lod=view->zoom/2+1;float span=lod==1?500:lod==2?2500:12500;
    atomic_store(&wanted_world,view->world);atomic_store(&wanted_lod,lod);
    atomic_store(&wanted_x,(int)floorf(view->x/span));atomic_store(&wanted_z,(int)floorf(view->z/span));
    atomic_store(&wanted,view->serial);pending_request=view->world>=0;pending_job=(map_job_t){.view=*view,.requested=esp_timer_get_time()};
    if(view->world<0){
        atomic_store(&map_busy,true);
        map_reply_t old;while(xQueueReceive(results,&old,0)==pdTRUE)discard_reply(&old);
        xQueueOverwrite(requests,&pending_job);xTaskNotifyGive(worker);
    }
    return true;
}
static void save_game(uint64_t dex,int best){
    if(!saves||!ensure_worker())return;
    game_save_t save={dex,best};xQueueOverwrite(saves,&save);xTaskNotifyGive(worker);
}
void app_service_init(void){
    map_perf_init();
    requests=xQueueCreate(1,sizeof(map_job_t));results=xQueueCreate(1,sizeof(map_reply_t));saves=xQueueCreate(1,sizeof(game_save_t));
    uint64_t dex=0;int32_t best=0;nvs_handle_t h;
    if(nvs_open("badge_apps",NVS_READONLY,&h)==ESP_OK){nvs_get_u64(h,"dex",&dex);nvs_get_i32(h,"best",&best);nvs_close(h);}
    badge_apps_saved(dex,best);badge_apps_bind(map_request,save_game);
}
bool app_service_map_busy(void){return atomic_load(&map_busy);}
void app_service_poll(void){
    if(lane_memory(false,false,NULL)){
        bool already=atomic_exchange(&shed_secondary,true);
        if(!already&&worker&&!app_service_map_busy())xTaskNotifyGive(worker);
    }
    map_reply_t reply;
    if(results&&badge_apps_map_accepting()&&xQueueReceive(results,&reply,0)==pdTRUE){
        static uint32_t gui_serial;static int64_t gui_total;
        if(gui_serial!=reply.map.view.serial){gui_serial=reply.map.view.serial;gui_total=0;}
        reply.perf.gui_start=esp_timer_get_time();bool accepted=badge_apps_map_result(&reply.map);
        reply.perf.end=esp_timer_get_time();gui_total+=reply.perf.end-reply.perf.gui_start;reply.perf.gui_us=gui_total;
        if(!accepted)reply.perf.result=-2;
        if(reply.perf.refresh)map_perf_publish(&reply.perf);
    }
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1536*1024)ui_memory_pressure_set(UI_MEMORY_CRITICAL);
    if(pending_request&&!app_service_map_busy()&&!badge_ui_transition_active()&&!physical_display_transition_active()){
        /* The GUI remains responsive while waiting for actual display ownership and RAM. */
        if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<40*1024||heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)<32*1024)return;
        physical_display_wait();atomic_store(&map_busy,true);pending_request=false;
        xQueueOverwrite(requests,&pending_job);xTaskNotifyGive(worker);
    }
    int page,loading,error,tiles_count,players_count;badge_apps_status(&page,&loading,&error,&tiles_count,&players_count);
    if((page<1||page==2||page==4)&&!app_service_map_busy()&&!pending_request&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>2*1024*1024&&ui_memory_pressure_get()!=UI_MEMORY_NORMAL)ui_memory_pressure_set(UI_MEMORY_NORMAL);
}
