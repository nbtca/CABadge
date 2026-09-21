#ifndef JX_USB_SCREEN_PROTOCOL_H
#define JX_USB_SCREEN_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define JX_WIDTH 360
#define JX_HEIGHT 360
#define JX_ROWS 16
#define JX_MAX_PAYLOAD (8+JX_WIDTH*JX_ROWS*2)
#define JX_HEADER 12
enum { JX_HELLO=1,JX_READY,JX_RECT,JX_TOUCH,JX_COMMAND,JX_FRAME,JX_HEARTBEAT,JX_TEXT,JX_KEY };
enum { JX_WALL_BEGIN=10,JX_WALL_CHUNK,JX_WALL_FINISH,JX_WALL_CANCEL,JX_WALL_ACK,JX_WALL_INFO,JX_WALL_HOTSPOT,JX_MANAGEMENT };
enum { JX_PAGE=1,JX_SLEEP,JX_MOTION };
static inline uint16_t jx_u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t jx_u32(const uint8_t *p){return jx_u16(p)|((uint32_t)jx_u16(p+2)<<16);}
static inline void jx_put16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static inline void jx_put32(uint8_t *p,uint32_t v){jx_put16(p,v);jx_put16(p+2,v>>16);}
static inline uint32_t jx_crc(uint32_t crc,const uint8_t *p,size_t n){
    while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return crc;
}
static inline size_t jx_packet(uint8_t *out,uint8_t type,const void *payload,size_t n){
    if(n>JX_MAX_PAYLOAD)return 0;
    memcpy(out,"JXUI",4);out[4]=1;out[5]=type;jx_put16(out+6,(uint16_t)n);
    if(n)memcpy(out+JX_HEADER,payload,n);
    uint32_t crc=jx_crc(~0u,out+4,4);crc=jx_crc(crc,out+JX_HEADER,n);
    jx_put32(out+8,~crc);return JX_HEADER+n;
}
typedef struct {uint8_t bytes[JX_HEADER+JX_MAX_PAYLOAD];size_t used;} jx_parser_t;
typedef void (*jx_receiver_t)(uint8_t,const uint8_t*,size_t,void*);
static inline void jx_feed(jx_parser_t *s,const uint8_t *bytes,size_t n,jx_receiver_t receive,void *ctx){
    for(size_t i=0;i<n;i++){
        if(s->used==sizeof(s->bytes))s->used=0;
        s->bytes[s->used++]=bytes[i];
        while(s->used>=4&&memcmp(s->bytes,"JXUI",4)){memmove(s->bytes,s->bytes+1,--s->used);}
        if(s->used<JX_HEADER)continue;
        size_t len=jx_u16(s->bytes+6);
        if(s->bytes[4]!=1||len>JX_MAX_PAYLOAD){memmove(s->bytes,s->bytes+1,--s->used);continue;}
        if(s->used<JX_HEADER+len)continue;
        uint32_t crc=jx_crc(~0u,s->bytes+4,4);crc=jx_crc(crc,s->bytes+JX_HEADER,len);
        if(~crc==jx_u32(s->bytes+8))receive(s->bytes[5],s->bytes+JX_HEADER,len,ctx);
        s->used=0;
    }
}
static inline bool jx_rect_valid(const uint8_t *p,size_t n){
    if(n<8)return false;
    unsigned x=jx_u16(p),y=jx_u16(p+2),w=jx_u16(p+4),h=jx_u16(p+6);
    return w&&h&&h<=JX_ROWS&&x+w<=JX_WIDTH&&y+h<=JX_HEIGHT&&n==8+w*h*2;
}
#endif
