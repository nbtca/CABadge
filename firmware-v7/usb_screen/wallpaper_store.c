#include "wallpaper_store.h"
#include "protocol.h"
#include "wallpaper_timing.h"

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
    *record=(wall_record_t){slot,jx_u32(header+8),size,~crc,WALL_STATIC,0};return true;
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
static unsigned library_span(uint32_t size){return (WALL_DATA+size+WALL_LIBRARY_SLOT-1)/WALL_LIBRARY_SLOT;}
static void library_header(uint8_t h[24],const wall_record_t *r,const char *magic){
    memcpy(h,magic,4);jx_put32(h+4,1);jx_put32(h+8,r->generation);
    jx_put32(h+12,r->size);jx_put32(h+16,r->crc);jx_put32(h+20,wall_crc(h,20));
}
int wall_library_find(const wall_record_t records[WALL_LIBRARY_COUNT],uint32_t size){
    if(!size||size>WALL_EAF_MAX)return -1;
    unsigned need=library_span(size),run=0;
    for(int i=0;i<WALL_LIBRARY_COUNT;i++){run=records[i].slot==-1?run+1:0;if(run==need)return i+1-(int)need;}
    return -1;
}
bool wall_library_scan(wall_record_t records[WALL_LIBRARY_COUNT]){
    for(int i=0;i<WALL_LIBRARY_COUNT;i++)records[i]=(wall_record_t){.slot=-1};
    for(int i=0;i<WALL_LIBRARY_COUNT;i++){
        uint8_t h[24],reservation[24];
        if(!wall_read(library_base(i),h,sizeof(h))||!wall_read(library_base(i)+32,reservation,sizeof(reservation)))return false;
        bool eaf=!memcmp(h,"JXWE",4);uint32_t size=jx_u32(h+12);
        bool valid=(eaf||!memcmp(h,"JXWL",4))&&jx_u32(h+4)==1&&size>0&&size<=WALL_EAF_MAX&&(eaf||size==WALL_BYTES)&&wall_crc(h,20)==jx_u32(h+20);
        if(valid&&library_span(size)<=(unsigned)(WALL_LIBRARY_COUNT-i)){
            unsigned span=library_span(size);
            records[i]=(wall_record_t){i,jx_u32(h+8),size,jx_u32(h+16),eaf?WALL_EAF:WALL_STATIC,0};
            if(eaf){
                uint8_t timing[WALL_TIMING_MAX];
                if(!wall_read(library_base(i)+WALL_TIMING_OFFSET,timing,WALL_TIMING_HEADER))return false;
                unsigned bytes=jx_u32(timing+16);
                if(!memcmp(timing,"JXWT",4)&&bytes<=WALL_TIMING_MAX-WALL_TIMING_HEADER){
                    if(!wall_read(library_base(i)+WALL_TIMING_OFFSET,timing,WALL_TIMING_HEADER+bytes))return false;
                    if(wall_timing_valid(timing,WALL_TIMING_HEADER+bytes,0,records[i].crc))records[i].timing_crc=jx_u32(timing+24);
                }
            }
            for(unsigned j=1;j<span;j++)records[i+j]=(wall_record_t){.slot=-2};
            i+=(int)span-1;
        }else if(!memcmp(reservation,"JXWR",4)&&jx_u32(reservation+4)==1&&wall_crc(reservation,20)==jx_u32(reservation+20)){
            size=jx_u32(reservation+12);
            if(size&&size<=WALL_EAF_MAX&&library_span(size)<=(unsigned)(WALL_LIBRARY_COUNT-i)){
                unsigned span=library_span(size);
                /* Interrupted upload: erase tail first, marker last; reboot may retry safely. */
                for(unsigned j=span;j>0;j--)if(!wall_erase(library_base(i+j-1),WALL_LIBRARY_SLOT))return false;
                i+=(int)span-1;
            }
        }
    }
    return true;
}
bool wall_library_reserve(wall_record_t records[WALL_LIBRARY_COUNT],uint32_t size,uint32_t crc,wall_record_t *pending){
    int slot=wall_library_find(records,size);if(slot<0)return false;
    uint32_t generation=0;for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(records[i].generation>generation)generation=records[i].generation;
    unsigned span=library_span(size);
    if(!wall_erase(library_base(slot),WALL_DATA))return false;
    wall_record_t r={slot,generation+1,size,crc,WALL_EAF,0};uint8_t h[24];library_header(h,&r,"JXWR");
    if(!wall_write(library_base(slot)+32,h,sizeof(h)))return false;
    for(unsigned j=0;j<span;j++)records[slot+j]=(wall_record_t){.slot=-2};
    *pending=r;return true;
}
bool wall_library_write_chunk(const wall_record_t *r,uint32_t offset,const void *data,size_t n){
    if(r->slot<0||r->slot>=WALL_LIBRARY_COUNT||offset>r->size||n>r->size-offset)return false;
    /* Sequential writer: erase each sector just before its first bytes arrive,
     * rather than stalling the USB/GUI task for a multi-megabyte erase at begin. */
    const uint8_t *p=data;
    while(n){
        uint32_t pos=library_base(r->slot)+WALL_DATA+offset;
        size_t step=4096-(pos&4095u);if(step>n)step=n;
        if(!(pos&4095u)&&!wall_erase(pos,4096))return false;
        if(!wall_write(pos,p,step))return false;
        offset+=step;p+=step;n-=step;
    }
    return true;
}
bool wall_library_write_timing(wall_record_t *r,const uint8_t *data,size_t n){
    if(r->slot<0||r->slot>=WALL_LIBRARY_COUNT||r->type!=WALL_EAF||!wall_timing_valid(data,n,0,r->crc))return false;
    if(!wall_write(library_base(r->slot)+WALL_TIMING_OFFSET,data,n))return false;
    r->timing_crc=jx_u32(data+24);return true;
}
bool wall_library_publish(wall_record_t records[WALL_LIBRARY_COUNT],const wall_record_t *r){
    uint8_t h[24];library_header(h,r,"JXWE");
    if(!wall_write(library_base(r->slot),h,sizeof(h)))return false;
    records[r->slot]=*r;return true;
}
bool wall_library_abort(wall_record_t records[WALL_LIBRARY_COUNT],const wall_record_t *r){
    if(r->slot<0||r->slot>=WALL_LIBRARY_COUNT||!r->size||r->size>WALL_EAF_MAX)return false;
    unsigned span=library_span(r->size);if(span>(unsigned)(WALL_LIBRARY_COUNT-r->slot))return false;
    for(unsigned j=span;j>0;j--)if(!wall_erase(library_base(r->slot+j-1),WALL_LIBRARY_SLOT))return false;
    int slot=r->slot;for(unsigned j=0;j<span;j++)records[slot+j]=(wall_record_t){.slot=-1};
    return true;
}
bool wall_library_read(const wall_record_t *record,uint8_t *pixels){
    return record->slot>=0&&record->slot<WALL_LIBRARY_COUNT&&record->size>0&&record->size<=WALL_EAF_MAX&&pixels&&
        wall_read(library_base(record->slot)+WALL_DATA,pixels,record->size)&&wall_crc(pixels,record->size)==record->crc;
}
bool wall_library_add(wall_record_t records[WALL_LIBRARY_COUNT],const uint8_t *pixels,uint32_t crc,int *slot){
    return wall_library_add_typed(records,pixels,WALL_BYTES,crc,WALL_STATIC,slot);
}
bool wall_library_add_typed(wall_record_t records[WALL_LIBRARY_COUNT],const uint8_t *pixels,uint32_t size,uint32_t crc,wall_type_t type,int *slot){
    if(!pixels||!size||size>WALL_LIBRARY_SLOT-WALL_DATA||(type!=WALL_STATIC&&type!=WALL_EAF)||(type==WALL_STATIC&&size!=WALL_BYTES)||wall_crc(pixels,size)!=crc)return false;
    int free_slot=-1;uint32_t generation=0;
    for(int i=0;i<WALL_LIBRARY_COUNT;i++){if(records[i].slot==-1&&free_slot<0)free_slot=i;if(records[i].generation>generation)generation=records[i].generation;}
    if(free_slot<0)return false;
    uint32_t base=library_base(free_slot);
    if(!wall_erase(base,WALL_LIBRARY_SLOT))return false;
    uint8_t verify[1024];uint32_t check=~0u;
    for(uint32_t i=0;i<size;i+=sizeof(verify)){
        size_t n=size-i<sizeof(verify)?size-i:sizeof(verify);
        if(!wall_write(base+WALL_DATA+i,pixels+i,n)||!wall_read(base+WALL_DATA+i,verify,n))return false;
        check=jx_crc(check,verify,n);
    }
    if(~check!=crc)return false;
    uint8_t h[24];memcpy(h,type==WALL_EAF?"JXWE":"JXWL",4);jx_put32(h+4,1);jx_put32(h+8,generation+1);
    jx_put32(h+12,size);jx_put32(h+16,crc);jx_put32(h+20,wall_crc(h,20));
    /* The header is written last. Existing images are never erased by upload. */
    if(!wall_write(base,h,sizeof(h)))return false;
    records[free_slot]=(wall_record_t){free_slot,generation+1,size,crc,type,0};*slot=free_slot;return true;
}
bool wall_library_delete(wall_record_t records[WALL_LIBRARY_COUNT],int slot){
    if(slot<0||slot>=WALL_LIBRARY_COUNT||records[slot].slot<0)return false;
    return wall_library_abort(records,&records[slot]);
}
bool wall_library_migrate(wall_record_t records[WALL_LIBRARY_COUNT],uint8_t *scratch){
    uint8_t marker[4];if(!wall_read(0xff000,marker,4))return false;
    if(!memcmp(marker,"LIB1",4))return true;
    wall_record_t old;if(!wall_load(&old,scratch))return false;
    if(old.size){
        bool found=false;
        for(int i=0;i<WALL_LIBRARY_COUNT;i++)if(records[i].slot>=0&&records[i].type==WALL_STATIC&&records[i].crc==old.crc&&wall_library_read(&records[i],scratch)){found=true;break;}
        /* A failed prior import may have committed its header already. */
        if(!found){if(!wall_load(&old,scratch))return false;int slot;if(!wall_library_add(records,scratch,old.crc,&slot))return false;}
    }
    return wall_erase(0xff000,4096)&&wall_write(0xff000,"LIB1",4);
}
