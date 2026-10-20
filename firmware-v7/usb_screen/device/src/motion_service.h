#ifndef CABADGE_MOTION_SERVICE_H
#define CABADGE_MOTION_SERVICE_H
#include <stddef.h>
void motion_service_init(void);
void motion_service_poll(void);
void motion_service_info(char *out,size_t size); /* GUI-owned read-only snapshot */
#endif
