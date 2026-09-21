#include "management_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    mg_session_t s={0};uint8_t token[16],other[16]={0};
    CHECK(mg_parse_key("0123456789abcdef0123456789abcdef",token));
    CHECK(!mg_parse_key("0123456789abcdef",other));CHECK(!mg_parse_key("x123456789abcdef0123456789abcdef",other));
    CHECK(!mg_authorized(&s,token,1));mg_open(&s,token,100);
    uint32_t generation=s.generation;
    CHECK(mg_authorized(&s,token,100));CHECK(!mg_authorized(&s,other,100));
    CHECK(mg_authorized(&s,token,300099));CHECK(!mg_authorized(&s,token,300100));
    mg_close(&s);CHECK(!mg_authorized(&s,token,101));CHECK(s.generation!=generation);
    mg_open(&s,other,102);CHECK(!mg_authorized(&s,token,103));CHECK(mg_authorized(&s,other,103));
    uint8_t command[MG_MAX_COMMAND]={1,MG_BRIGHTNESS,70};
    CHECK(mg_valid_command(command,3));command[2]=9;CHECK(!mg_valid_command(command,3));command[2]=101;CHECK(!mg_valid_command(command,3));
    command[1]=MG_WIFI_ENABLE;command[2]=2;CHECK(!mg_valid_command(command,3));command[2]=1;CHECK(mg_valid_command(command,3));CHECK(!mg_valid_command(command,4));
    command[1]=MG_WIFI_SCAN;CHECK(mg_valid_command(command,2));CHECK(!mg_valid_command(command,3));
    command[1]=99;CHECK(!mg_valid_command(command,2));command[0]=2;CHECK(!mg_valid_command(command,3));
    command[0]=1;command[1]=MG_WIFI_CONNECT;command[2]=32;command[3]=63;memset(command+4,'a',95);
    CHECK(mg_valid_command(command,99));CHECK(!mg_valid_command(command,98));CHECK(!mg_valid_command(command,100));
    command[4]=0;CHECK(!mg_valid_command(command,99));command[4]='a';command[36]=128;CHECK(!mg_valid_command(command,99));
    command[2]=1;command[3]=0;CHECK(mg_valid_command(command,5));command[3]=7;CHECK(!mg_valid_command(command,12));
    command[1]=MG_BLE_CONNECT;command[2]=1;CHECK(mg_valid_command(command,9));command[2]=2;CHECK(!mg_valid_command(command,9));
    const uint8_t utf8[]={0xe4,0xb8,0xad};CHECK(mg_text(utf8,3,false));CHECK(!mg_text(utf8,2,false));CHECK(!mg_text(utf8,3,true));
    const uint8_t invalid[]={0xc0,0x80};CHECK(!mg_text(invalid,2,false));
    puts("management: authorization expiry/revocation/rotation, version, bounds and commands PASS");
}
