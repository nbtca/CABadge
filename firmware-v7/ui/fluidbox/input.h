#pragma once
#include <math.h>
#include "config.h"
#include "sim.h"

typedef struct { float lp[3]; bool primed; } fluidbox_input_t;

/* SC7A20 input is already mapped to right/down/face-up by motion_service. */
static inline sim_forces_t fluidbox_input(fluidbox_input_t *input, const int16_t mg[3], float dt)
{
    float a[3];
    const float k = 1.0f - expf(-6.2831853f * GRAVITY_LP_HZ * dt);
    for (int i = 0; i < 3; i++) {
        a[i] = mg[i] * .001f;
        if (!input->primed || dt > .25f) input->lp[i] = a[i];
        else input->lp[i] += k * (a[i] - input->lp[i]);
    }
    input->primed = true;
    const float mag = sqrtf(input->lp[0]*input->lp[0] + input->lp[1]*input->lp[1] + input->lp[2]*input->lp[2]);
    sim_forces_t out = {0};
    for (int i = 0; i < 3; i++) {
        const float direction = mag > .05f ? input->lp[i] / mag : (i == 1 ? 1.f : 0.f);
        out.gravity[i] = GRAVITY_MPS2 * PX_PER_METER *
            (GRAVITY_GAIN * direction + SHAKE_GAIN * (a[i] - input->lp[i]));
    }
    // shortcut: SC7A20 has no gyroscope; omega/alpha stay zero until hardware gains one.
    return out;
}
