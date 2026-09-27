#pragma once
#include "protocol.h"
/* Bounded first profile: opaque 360x360, 8-bit palette, RLE, <=360 frames,
 * <=24 rows/block. Validate all lengths before the component parses headers.
 * No custom decoder: actual pixels are decoded by esp_lv_eaf_player. */
static inline bool eaf_validate(const void *data,size_t size){
    const uint8_t *p=data;if(!p||size<24||size>0x300000||memcmp(p,"\x89" "EAF",4))return false;
    uint32_t frames=jx_u32(p+4),bytes=jx_u32(p+12),sum=0;
    if(!frames||frames>360||bytes!=size-16||frames*8>bytes)return false;
    for(size_t i=16;i<size;i++)sum+=p[i];
    if(sum!=jx_u32(p+8))return false;
    size_t base=16+frames*8;
    for(unsigned i=0;i<frames;i++){
        uint32_t len=jx_u32(p+16+i*8),off=jx_u32(p+20+i*8);
        if(off>size-base||len>size-base-off||len<20)return false;
        const uint8_t *f=p+base+off;if(jx_u16(f)!=0x5a5a)return false;f+=2;len-=2;
        if(memcmp(f,"_S",2)||f[9]!=8||jx_u16(f+10)!=360||jx_u16(f+12)!=360)return false;
        unsigned blocks=jx_u16(f+14),rows=jx_u16(f+16);
        if(!rows||rows>24||blocks!=(360+rows-1)/rows)return false;
        size_t at=18+blocks*4,header=at+1024;if(header>len)return false;
        for(unsigned c=0;c<256;c++)if(f[at+c*4+3]!=255)return false;
        at=header;
        for(unsigned b=0;b<blocks;b++){
            unsigned n=jx_u32(f+18+b*4),pixels=0,h=360-b*rows;if(h>rows)h=rows;
            if(n<3||n>len-at||f[at]!=0||(n-1)%2)return false;
            for(unsigned k=1;k<n;k+=2){if(!f[at+k])return false;pixels+=f[at+k];if(pixels>360*h)return false;}
            if(pixels!=360*h)return false;
            at+=n;
        }
        if(at!=len)return false;
    }
    return true;
}
