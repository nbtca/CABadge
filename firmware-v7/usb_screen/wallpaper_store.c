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

static uint32_t library_base(int slot){return WALL_LIBRARY_BASE+(uint32_t)slot*WALL_LIBRARY_SLOT;}
bool wall_library_scan(wall_record_t records[WALL_LIBRARY_COUNT]){
    for(int i=0;i<WALL_LIBRARY_COUNT;i++){
        uint8_t h[24];records[i]=(wall_record_t){.slot=-1};
        if(!wall_read(library_base(i),h,sizeof(h)))return false;
        if(!memcmp(h,"JXWL",4)&&jx_u32(h+4)==1&&jx_u32(h+12)==WALL_BYTES&&wall_crc(h,20)==jx_u32(h+20))
            records[i]=(wall_record_t){i,jx_u32(h+8),WALL_BYTES,jx_u32(h+16)};
    }
    return true;
}
bool wall_library_read(const wall_record_t *record,uint8_t *pixels){
    return record->slot>=0&&record->slot<WALL_LIBRARY_COUNT&&record->size==WALL_BYTES&&pixels&&
        wall_read(library_base(record->slot)+WALL_DATA,pixels,WALL_BYTES)&&wall_crc(pixels,WALL_BYTES)==record->crc;
}
bool wall_library_add(wall_record_t records[WALL_LIBRARY_COUNT],const uint8_t *pixels,uint32_t crc,int *slot){
    if(!pixels||wall_crc(pixels,WALL_BYTES)!=crc)return false;
    int free_slot=-1;uint32_t generation=0;
    for(int i=0;i<WALL_LIBRARY_COUNT;i++){if(records[i].slot<0&&free_slot<0)free_slot=i;if(records[i].generation>generation)generation=records[i].generation;}
    if(free_slot<0)return false;
    uint32_t base=library_base(free_slot);
    if(!wall_erase(base,WALL_LIBRARY_SLOT))return false;
    uint8_t verify[1024];uint32_t check=~0u;
    for(uint32_t i=0;i<WALL_BYTES;i+=sizeof(verify)){
        size_t n=WALL_BYTES-i<sizeof(verify)?WALL_BYTES-i:sizeof(verify);
        if(!wall_write(base+WALL_DATA+i,pixels+i,n)||!wall_read(base+WALL_DATA+i,verify,n))return false;
        check=jx_crc(check,verify,n);
    }
    if(~check!=crc)return false;
    uint8_t h[24];memcpy(h,"JXWL",4);jx_put32(h+4,1);jx_put32(h+8,generation+1);
    jx_put32(h+12,WALL_BYTES);jx_put32(h+16,crc);jx_put32(h+20,wall_crc(h,20));
    /* The header is written last. Existing images are never erased by upload. */
    if(!wall_write(base,h,sizeof(h)))return false;
    records[free_slot]=(wall_record_t){free_slot,generation+1,WALL_BYTES,crc};*slot=free_slot;return true;
}
bool wall_library_delete(wall_record_t records[WALL_LIBRARY_COUNT],int slot){
    if(slot<0||slot>=WALL_LIBRARY_COUNT||records[slot].slot<0)return false;
    if(!wall_erase(library_base(slot),WALL_DATA))return false;
    records[slot]=(wall_record_t){.slot=-1};return true;
}
bool wall_library_migrate(wall_record_t records[WALL_LIBRARY_COUNT],uint8_t *scratch){
    uint8_t marker[4];if(!wall_read(0xff000,marker,4))return false;
    if(!memcmp(marker,"LIB1",4))return true;
    wall_record_t old;if(!wall_load(&old,scratch))return false;
    if(old.size){
        bool found=false;
        for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(records[i].slot>=0&&records[i].crc==old.crc&&wall_library_read(&records[i],scratch)){found=true;break;}
        /* A failed prior import may have committed its header already. */
        if(!found){if(!wall_load(&old,scratch))return false;int slot;if(!wall_library_add(records,scratch,old.crc,&slot))return false;}
    }
    return wall_erase(0xff000,4096)&&wall_write(0xff000,"LIB1",4);
}
