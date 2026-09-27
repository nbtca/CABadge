#ifndef CABADGE_MAP_PERF_H
#define CABADGE_MAP_PERF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {MAP_TILE_HTTP=-10,MAP_TILE_READ=-11,MAP_TILE_TIMEOUT=-12,MAP_TILE_CONNECTION=-13,
    MAP_TILE_PNG_PARSE=-14,MAP_TILE_CRC=-15,MAP_TILE_DECODE=-16,MAP_TILE_SIZE=-17,MAP_TILE_OTHER=-18};

/* Monotonic microseconds since boot; zero milestone means not observed.
 * Streaming decode includes downsample and overlaps the download interval. */
typedef struct {
    uint32_t seq,serial,bytes;
    int world,zoom,lod,x,z,status,result;
    float center_x,center_z;
    bool refresh,hit,reused,persistent,patch_failed,background;
    int lane;
    unsigned connects,reuse_count,updates,max_inflight,expired;
    unsigned requested_lanes,actual_lanes,internal_free,dma_free,psram_free;
    const char *fallback_reason; /* Static literal; safe in queued/ring copies. */
    const char *error_category,*error_detail; /* Static literals from this firmware. */
    int http_errno;
    int64_t network_wall_us;
    unsigned needed,hits,misses,downloaded;
    int64_t start,request,first_byte,download_done,png_done,end;
    int64_t network_us,decode_us,compose_us,players_us,worker_done,gui_start,gui_us;
} map_perf_t;
void map_perf_init(void);
void map_perf_publish(map_perf_t *record);
bool map_perf_query(uint32_t after,char *out,size_t capacity);
#endif
