#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned calls;
static void receive(uint8_t type,const uint8_t *data,size_t n,void *ctx){
    (void)ctx;CHECK(type==JX_TOUCH&&n==5);CHECK(jx_u16(data)==123&&jx_u16(data+2)==234&&data[4]==1);calls++;
}
int main(void){
    const uint8_t expected[]={0x4a,0x58,0x55,0x49,1,4,5,0,0x8e,0x95,0x70,0x7a,0x7b,0,0xea,0,1};
    uint8_t buffer[JX_HEADER+JX_MAX_PAYLOAD],touch[]={123,0,234,0,1};
    size_t n=jx_packet(buffer,JX_TOUCH,touch,5);CHECK(n==sizeof(expected)&&!memcmp(buffer,expected,n));
    jx_parser_t parser={0};
    jx_feed(&parser,(const uint8_t*)"boot log\r\n",10,receive,NULL);
    for(size_t i=0;i<n;i++)jx_feed(&parser,buffer+i,1,receive,NULL);
    CHECK(calls==1);buffer[12]^=1;jx_feed(&parser,buffer,n,receive,NULL);CHECK(calls==1);
    buffer[12]^=1;jx_feed(&parser,buffer,n,receive,NULL);CHECK(calls==2);
    CHECK(!jx_packet(buffer,JX_RECT,NULL,JX_MAX_PAYLOAD+1));
    uint8_t rect[10]={0};jx_put16(rect,359);jx_put16(rect+2,359);jx_put16(rect+4,1);jx_put16(rect+6,1);
    CHECK(jx_rect_valid(rect,sizeof(rect)));jx_put16(rect,360);CHECK(!jx_rect_valid(rect,sizeof(rect)));
    puts("PASS: C/Python wire vector, fragmentation, CRC rejection, resync and rectangle bounds");return 0;
}
