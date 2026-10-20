#pragma once

#include "sim.h"
#include <stdbool.h>

// Rasterizes particle discs with perspective projection, or a continuous
// density surface with orthographic projection, into staged frame bands.
// The display layer owns frame staging and scan synchronization; see display.h.

void render_init(void);

typedef enum { RENDER_PARTICLES, RENDER_LIQUID, RENDER_WATER, RENDER_LAVA, RENDER_UKIYOE, RENDER_MODE_COUNT } render_mode_t;

// Requests a mode atomically; the renderer reads the latest request at frame start.
void render_set_mode(render_mode_t mode);

// Sets the gravity input used by sim_step(), including shake acceleration.
// Liquid lighting follows its smoothed opposite direction in view space.
// Components are atomic but do not form a coherent three-component snapshot.
void render_set_gravity(const float gravity[3]);

// Elapsed-time measurements in microseconds, with staged byte and band counts.
typedef struct { uint32_t cpu_us, prep_us, stage_us, slot_wait_us, bytes, bands; } render_stats_t;

// Renders a snapshot and queues it for asynchronous transfer. full_width disables
// horizontal cropping for comparisons; empty bands may still be skipped.
// prep_us covers snapshot, projection and surface fields and is included in cpu_us.
// stage_us measures band copies; slot_wait_us measures display_present(), including
// queue and slot waits. cpu_us excludes those two intervals but includes scheduling delays.
render_stats_t render_frame(bool full_width);
