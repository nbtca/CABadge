#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UI_COMPOSITOR_LAYERS 4
/* Immutable, LCD byte order RGB565. Ownership stays with the pinned cache. */
typedef struct {
    const uint8_t *pixels;
    uint32_t stride;
    int16_t width,height,x,y,radius;
} ui_compositor_layer_t;
typedef struct {
    int16_t x,y,width,height;
    uint16_t background;
    uint8_t count;
    ui_compositor_layer_t layers[UI_COMPOSITOR_LAYERS];
} ui_compositor_frame_t;
/* GUI task only. stop joins the worker before any source can be freed. */
bool ui_transition_compositor_begin(const ui_compositor_frame_t *frame);
void ui_transition_compositor_present(const ui_compositor_frame_t *frame);
void ui_transition_compositor_stop(void);
bool ui_transition_compositor_active(void);
void ui_transition_compositor_stats(char *out,size_t size);
/* Pure stride/clip compositor, shared by production and the host pixel check. */
size_t ui_transition_compositor_rows(const ui_compositor_frame_t *f,int first,int rows,uint8_t *out);

/* GUI owns start/stop; prepare runs ONLY on the existing compositor worker.
 * One mutable source is safe: prepare follows completion of the previous DMA.
 * Callback/context lifetime extends through stop acknowledgement. */
typedef bool (*ui_compositor_prepare_cb)(void *context,uint32_t *duration_us);
bool ui_transition_compositor_stream_begin(const ui_compositor_frame_t *frame,ui_compositor_prepare_cb prepare,void *context,uint32_t period_us);
bool ui_transition_compositor_stream_active(void);
bool ui_transition_compositor_stream_failed(void);
void ui_transition_compositor_stream_stats(char *out,size_t size);
