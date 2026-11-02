#include "map_perf.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdio.h>

/* Bounded history, not a per-chunk trace. Readers have independent cursors. */
#define HISTORY 64
static map_perf_t *records;
static uint32_t latest;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
void map_perf_init(void){records=heap_caps_calloc(HISTORY,sizeof(*records),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
void map_perf_publish(map_perf_t *r){
    if(!records)return;
    portENTER_CRITICAL(&lock);
    r->seq=++latest;records[(r->seq-1)%HISTORY]=*r;
    portEXIT_CRITICAL(&lock);
}
bool map_perf_query(uint32_t after,char *out,size_t cap){
    map_perf_t r={0};uint32_t last,oldest,lost=0;
    portENTER_CRITICAL(&lock);
    last=latest;oldest=last>=HISTORY?last-HISTORY+1:1;
    if(records&&after<last){
        uint32_t next=after+1;
        if(next<oldest){lost=oldest-next;next=oldest;}
        r=records[(next-1)%HISTORY];
    }
    portEXIT_CRITICAL(&lock);
    int n=snprintf(out,cap,"{\"map_perf\":1,\"available\":%s,\"latest\":%"PRIu32",\"lost\":%"PRIu32",\"record\":",
        records?"true":"false",last,lost);
    if(n<0||(size_t)n>=cap)return false;
    int k;
    if(!r.seq)k=snprintf(out+n,cap-n,"null}");
    else k=snprintf(out+n,cap-n,
        "{\"seq\":%"PRIu32",\"serial\":%"PRIu32",\"kind\":\"%s\",\"background\":%s,\"world\":%d,\"zoom\":%d,\"lod\":%d,\"x\":%d,\"z\":%d,"
        "\"center_x\":%.3f,\"center_z\":%.3f,\"hit\":%s,\"http_status\":%d,\"result\":%d,\"bytes\":%"PRIu32","
        "\"tiles_needed\":%u,\"cache_hits\":%u,\"cache_misses\":%u,\"tiles_downloaded\":%u,"
        "\"start_us\":%"PRId64",\"request_start_us\":%"PRId64",\"first_byte_us\":%"PRId64",\"download_complete_us\":%"PRId64","
        "\"png_complete_us\":%"PRId64",\"rgb565_complete_us\":%"PRId64",\"end_us\":%"PRId64","
        "\"network_us\":%"PRId64",\"decode_downsample_us\":%"PRId64",\"compose_us\":%"PRId64",\"players_us\":%"PRId64","
        "\"worker_done_us\":%"PRId64",\"gui_start_us\":%"PRId64",\"gui_submit_us\":%"PRId64","
        "\"lane\":%d,\"connects\":%u,\"reused\":%s,\"persistent\":%s,\"reuse_count\":%u,\"updates\":%u,\"max_inflight\":%u,\"network_wall_us\":%"PRId64","
        "\"requested_lanes\":%u,\"actual_lanes\":%u,\"fallback_reason\":\"%s\","
        "\"internal_free\":%u,\"dma_free\":%u,\"psram_free\":%u,\"expired_tiles\":%u,"
        "\"error_category\":\"%s\",\"error_detail\":\"%s\",\"http_errno\":%d}}",
        r.seq,r.serial,r.refresh?"refresh":"tile",r.background?"true":"false",r.world,r.zoom,r.lod,r.x,r.z,
        r.center_x,r.center_z,r.hit?"true":"false",r.status,r.result,r.bytes,
        r.needed,r.hits,r.misses,r.downloaded,r.start,r.request,r.first_byte,r.download_done,
        r.png_done,r.png_done,r.end,r.network_us,r.decode_us,r.compose_us,r.players_us,r.worker_done,r.gui_start,r.gui_us,
        r.lane,r.connects,r.reused?"true":"false",r.persistent?"true":"false",r.reuse_count,r.updates,r.max_inflight,r.network_wall_us,
        r.requested_lanes,r.actual_lanes,r.fallback_reason?r.fallback_reason:"NONE",
        r.internal_free,r.dma_free,r.psram_free,r.expired,
        r.error_category?r.error_category:"NONE",r.error_detail?r.error_detail:"NONE",r.http_errno);
    return k>=0&&(size_t)k<cap-(size_t)n;
}
