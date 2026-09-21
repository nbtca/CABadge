#pragma once
#include "physical_display.h"

/* The GUI owns the draw buffer until wait returns a completed DMA result. */
typedef struct { uint32_t flush_us,copy_us; lcd_profile_t dma; int error; bool last; } display_result_t;
void display_worker_init(void);
void display_worker_submit(const lv_area_t *area,const uint8_t *pixels,bool last);
display_result_t display_worker_wait(void);

bool display_worker_poll(display_result_t *result);
