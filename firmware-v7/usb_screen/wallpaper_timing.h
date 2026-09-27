#pragma once
#include "protocol.h"
/* CABadge resource header, NOT part of the official EAF payload. Little endian:
 * JXWT, u16 version/mode, u32 frames/period_us/table_bytes/eaf_crc/timing_crc,
 * then (PER_FRAME only) u16 delay_ms[frames]. CRC covers bytes 0..23 + table. */
#define WALL_TIMING_OFFSET 64u
#define WALL_TIMING_HEADER 28u
#define WALL_TIMING_MAX (WALL_TIMING_HEADER+360u*2u)
#define WALL_TIMING_DEFAULT_US 33333u
#define WALL_TIMING_MIN_US 10000u
#define WALL_TIMING_MAX_US 60000000u
static inline bool wall_timing_valid(const uint8_t *p,size_t n,uint32_t frames,uint32_t eaf_crc){
    if(!p||n<WALL_TIMING_HEADER||n>WALL_TIMING_MAX||memcmp(p,"JXWT",4)||jx_u16(p+4)!=1)return false;
    unsigned mode=jx_u16(p+6),count=jx_u32(p+8),bytes=jx_u32(p+16),period=jx_u32(p+12);
    if(!count||count>360||(frames&&count!=frames)||jx_u32(p+20)!=eaf_crc||n!=WALL_TIMING_HEADER+bytes)return false;
    if(period<WALL_TIMING_MIN_US||period>WALL_TIMING_MAX_US)return false;
    if(!((mode==1&&!bytes)||(mode==2&&bytes==count*2)))return false;
    if(mode==2)for(unsigned i=0;i<count;i++){unsigned ms=jx_u16(p+WALL_TIMING_HEADER+i*2);if(ms<10||ms>60000)return false;}
    return ~jx_crc(jx_crc(~0u,p,24),p+WALL_TIMING_HEADER,bytes)==jx_u32(p+24);
}
/* Called only with validated immutable metadata, or NULL for legacy resources. */
static inline uint32_t wall_timing_period(const uint8_t *p,unsigned frame){
    if(!p)return WALL_TIMING_DEFAULT_US;
    return jx_u16(p+6)==2&&frame<jx_u32(p+8)?(uint32_t)jx_u16(p+WALL_TIMING_HEADER+frame*2)*1000u:jx_u32(p+12);
}
