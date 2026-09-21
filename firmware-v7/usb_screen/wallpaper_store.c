#include "wallpaper_store.h"
#include "protocol.h"

uint32_t wall_crc(const void *data,size_t size){return ~jx_crc(~0u,data,size);}
static int valid_slot(int slot,wall_record_t *record){
    uint8_t header[24],buffer[1024];uint32_t base=slot*WALL_SLOT,crc=~0u;
    if(!wall_read(base,header,sizeof(header)))return -1;
    if(memcmp(header,"JXWP",4)||jx_u32(header+4)!=1||wall_crc(header,20)!=jx_u32(header+20))return 0;
    uint32_t size=jx_u32(header+12);
    if(size!=0&&size!=WALL_BYTES)return false;
    for(uint32_t i=0;i<size;i+=sizeof(buffer)){
        size_t n=size-i<sizeof(buffer)?size-i:sizeof(buffer);
        if(!wall_read(base+WALL_DATA+i,buffer,n))return -1;
        crc=jx_crc(crc,buffer,n);
    }
    if(~crc!=jx_u32(header+16))return false;
    *record=(wall_record_t){slot,jx_u32(header+8),size,~crc};return true;
}
bool wall_load(wall_record_t *record,uint8_t *pixels){
    wall_record_t a,b;int va=valid_slot(0,&a),vb=valid_slot(1,&b);
    *record=(wall_record_t){.slot=-1};
    if(va<0||vb<0)return false;
    if(!va&&!vb)return true; /* Empty storage uses the built-in picture. */
    *record=vb&&(!va||(int32_t)(b.generation-a.generation)>0)?b:a;
    return !record->size||wall_read(record->slot*WALL_SLOT+WALL_DATA,pixels,record->size);
}
bool wall_commit(wall_record_t *record,const uint8_t *pixels,uint32_t size,uint32_t crc){
    if((size!=0&&size!=WALL_BYTES)||(!pixels&&size)||wall_crc(pixels,size)!=crc)return false;
    int slot=record->slot==0?1:0;uint32_t base=slot*WALL_SLOT;
    if(!wall_erase(base,WALL_DATA+((size+4095u)&~4095u)))return false;
    uint8_t verify[1024];uint32_t check=~0u;
    for(uint32_t i=0;i<size;i+=sizeof(verify)){
        size_t n=size-i<sizeof(verify)?size-i:sizeof(verify);
        if(!wall_write(base+WALL_DATA+i,pixels+i,n)||!wall_read(base+WALL_DATA+i,verify,n))return false;
        check=jx_crc(check,verify,n);
    }
    if(~check!=crc)return false;
    uint8_t header[24];memcpy(header,"JXWP",4);jx_put32(header+4,1);
    jx_put32(header+8,record->generation+1);jx_put32(header+12,size);jx_put32(header+16,crc);jx_put32(header+20,wall_crc(header,20));
    /* Header is the commit marker. Never erase the previous valid slot. */
    if(!wall_write(base,header,sizeof(header)))return false;
    wall_record_t saved;
    if(valid_slot(slot,&saved)!=1)return false;
    *record=saved;return true;
}
