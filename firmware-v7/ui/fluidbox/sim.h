#pragma once

#include <stdint.h>
#include <stdbool.h>

// Per-particle state handed to the renderer. Positions are in simulation
// space: x and y in screen pixels, z in pixels of depth into the case.
typedef struct {
    float x, y, z;
    float speed;    // Simulation speed in px/s, used for colour selection.
    uint8_t foam;   // Side-wall impact foam, 0..255; decays once per substep.
} sim_particle_view_t;

// Linear acceleration and angular motion for one step, in simulation axes.
typedef struct {
    float gravity[3];  // px/s^2, gravity plus shake-induced inertial acceleration.
    float omega[3];    // rad/s, box angular velocity
    float alpha[3];    // rad/s^2, box angular acceleration
} sim_forces_t;

// Published solver diagnostics. Pass timings measure elapsed microseconds in
// the most recent substep, including any scheduling delays.
typedef struct {
    float mean_density;
    float rest_density;
    float mean_speed;
    float max_speed;
    int us_grid;
    int us_density;  // includes viscosity; the two share one pass
    int us_relax;
    int clamped;                  // All pairs capped at MAX_DISPLACEMENT in the last substep.
    int pairs;                    // neighbour pairs cached for relaxation
} sim_stats_t;

void sim_init(void);

// Re-seeds a slightly randomized block at the bottom with zero velocity and foam.
void sim_reset(void);

// Advances from dt_real seconds of elapsed time, applying TIME_SCALE and
// limiting each of SUBSTEPS substeps to SIM_DT_MAX.
void sim_step(float dt_real, const sim_forces_t *forces);

// Copies the most recently published particle state. Returns the count.
int sim_snapshot(sim_particle_view_t *out, int max);

void sim_stats(sim_stats_t *out);
