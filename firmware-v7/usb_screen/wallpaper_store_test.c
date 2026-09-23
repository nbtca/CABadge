#include "wallpaper_store.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t flash[WALL_LIBRARY_END],snapshot[WALL_LIBRARY_END],pixels[WALL_BYTES],restored[WALL_BYTES];
static int budget=-1;
static bool read_failure;
bool wall_read(uint32_t offset,void *data,size_t size){if(read_failure||offset+size>sizeof(flash))return false;memcpy(data,flash+offset,size);return true;}
bool wall_write(uint32_t offset,const void *data,size_t size){
    if(offset+size>sizeof(flash))return false;
    if(budget>=0&&budget<(int)size){memcpy(flash+offset,data,budget);budget=0;return false;}
    if(budget>=0)budget-=size;
    for(size_t i=0;i<size;i++)flash[offset+i]&=((const uint8_t*)data)[i];return true;
}
bool wall_erase(uint32_t offset,size_t size){if(offset%4096||size%4096||offset+size>sizeof(flash))return false;memset(flash+offset,255,size);return true;}
int main(void){
    memset(flash,255,sizeof(flash));wall_record_t record;
    CHECK(wall_load(&record,restored)&&record.slot==-1);
    CHECK(wall_crc("123456789",9)==0xcbf43926);
    read_failure=true;CHECK(!wall_load(&record,restored));read_failure=false;
    memset(pixels,0x73,sizeof(pixels));uint32_t crc=wall_crc(pixels,sizeof(pixels));
    CHECK(!wall_commit(&record,pixels,12,crc));CHECK(!wall_commit(&record,pixels,sizeof(pixels),crc+1));
    CHECK(wall_commit(&record,pixels,sizeof(pixels),crc));CHECK(wall_load(&record,restored)&&!memcmp(pixels,restored,sizeof(pixels)));
    memcpy(snapshot,flash,sizeof(flash));memset(pixels,0x96,sizeof(pixels));crc=wall_crc(pixels,sizeof(pixels));
    for(int cut=0;cut<(int)WALL_BYTES+24;cut+=1021){
        memcpy(flash,snapshot,sizeof(flash));CHECK(wall_load(&record,restored));budget=cut;
        CHECK(!wall_commit(&record,pixels,sizeof(pixels),crc));budget=-1;
        CHECK(wall_load(&record,restored)&&record.generation==1&&restored[0]==0x73);
    }
    for(int cut=0;cut<24;cut++){
        memcpy(flash,snapshot,sizeof(flash));CHECK(wall_load(&record,restored));budget=WALL_BYTES+cut;
        CHECK(!wall_commit(&record,pixels,sizeof(pixels),crc));budget=-1;CHECK(wall_load(&record,restored)&&record.generation==1);
    }
    CHECK(wall_commit(&record,pixels,sizeof(pixels),crc));CHECK(wall_load(&record,restored)&&restored[0]==0x96);
    flash[record.slot*WALL_SLOT+WALL_DATA+10]^=1;CHECK(wall_load(&record,restored)&&restored[0]==0x73);
    CHECK(wall_commit(&record,NULL,0,0));CHECK(wall_load(&record,restored)&&record.size==0);
    memset(flash,255,sizeof(flash));record=(wall_record_t){.slot=-1};
    memset(pixels,0x53,sizeof(pixels));crc=wall_crc(pixels,sizeof(pixels));
    CHECK(wall_commit(&record,pixels,WALL_BYTES,crc));
    wall_record_t library[WALL_LIBRARY_COUNT];CHECK(wall_library_scan(library));
    CHECK(wall_library_migrate(library,restored));CHECK(library[0].slot==0);
    CHECK(wall_library_read(&library[0],restored)&&restored[0]==0x53);
    CHECK(wall_library_migrate(library,restored)&&library[1].slot<0);
    memset(pixels,0x97,sizeof(pixels));crc=wall_crc(pixels,sizeof(pixels));int slot=-1;
    CHECK(wall_library_add(library,pixels,crc,&slot)&&slot==1);
    CHECK(wall_library_read(&library[0],restored)&&restored[0]==0x53);
    memcpy(snapshot,flash,sizeof(flash));
    for(int cut=0;cut<24;cut++){
        memcpy(flash,snapshot,sizeof(flash));CHECK(wall_library_scan(library));budget=WALL_BYTES+cut;
        CHECK(!wall_library_add(library,pixels,crc,&slot));budget=-1;
        CHECK(wall_library_scan(library)&&library[2].slot<0);
        CHECK(wall_library_read(&library[0],restored)&&restored[0]==0x53);
        CHECK(wall_library_read(&library[1],restored)&&restored[0]==0x97);
    }
    CHECK(wall_library_delete(library,0));CHECK(wall_library_scan(library)&&library[0].slot<0&&library[1].slot==1);
    CHECK(wall_library_migrate(library,restored)&&library[0].slot<0);
    CHECK(!wall_library_delete(library,-1)&&!wall_library_delete(library,31));
    CHECK(wall_library_add(library,pixels,crc,&slot)&&slot==0);
    for(int i=2;i<WALL_LIBRARY_COUNT;i++)CHECK(wall_library_add(library,pixels,crc,&slot)&&slot==i);
    CHECK(!wall_library_add(library,pixels,crc,&slot));
    CHECK(wall_library_read(&library[1],restored)&&restored[0]==0x97);
    puts("wallpaper library: migration, no re-import after delete, append, 31-image capacity, bounds and interrupted commits PASS");
    puts("wallpaper storage: upload, reboot, corruption fallback, interrupted writes and persistent reset PASS");return 0;
}
