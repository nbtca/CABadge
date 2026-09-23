#ifndef JX_WALLPAPER_STORE_H
#define JX_WALLPAPER_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define WALL_BYTES (360u*360u*2u)
#define WALL_SLOT 0x80000u
#define WALL_DATA 0x1000u
/* Backend returns false on any incomplete operation. Callers serialize access. */
bool wall_read(uint32_t offset,void *data,size_t size);
bool wall_write(uint32_t offset,const void *data,size_t size);
bool wall_erase(uint32_t offset,size_t size);
typedef struct {int slot;uint32_t generation,size,crc;} wall_record_t;
uint32_t wall_crc(const void *data,size_t size);
bool wall_load(wall_record_t *record,uint8_t *pixels);
bool wall_commit(wall_record_t *record,const uint8_t *pixels,uint32_t size,uint32_t crc);
/* Keep the legacy 1 MB untouched for migration/rollback. */
#define WALL_LIBRARY_BASE 0x100000u
#define WALL_LIBRARY_SLOT 0x41000u
#define WALL_LIBRARY_COUNT 31
#define WALL_LIBRARY_END (WALL_LIBRARY_BASE+WALL_LIBRARY_SLOT*WALL_LIBRARY_COUNT)
bool wall_library_scan(wall_record_t records[WALL_LIBRARY_COUNT]);
bool wall_library_read(const wall_record_t *record,uint8_t *pixels);
bool wall_library_add(wall_record_t records[WALL_LIBRARY_COUNT],const uint8_t *pixels,uint32_t crc,int *slot);
bool wall_library_delete(wall_record_t records[WALL_LIBRARY_COUNT],int slot);
bool wall_library_migrate(wall_record_t records[WALL_LIBRARY_COUNT],uint8_t *scratch);
#endif
