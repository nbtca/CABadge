#include "sim.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Grid cells must span at least one smoothing radius on every axis.
#define GRID_CX 17
#define GRID_CY 17
#define GRID_CZ 2
#define GRID_CELLS (GRID_CX * GRID_CY * GRID_CZ)

#define CELL_INDEX(cx, cy, cz) ((((cz) * GRID_CY) + (cy)) * GRID_CX + (cx))

#define WALL_MARGIN 3.75f

static const char *TAG = "sim";

typedef struct {
    float pos[PARTICLE_MAX][3];
    float vel[PARTICLE_MAX][3];
    float old[PARTICLE_MAX][3];
    uint8_t foam[PARTICLE_MAX];  // Decaying side-wall impact foam, preserved during particle reordering.
} particles_t;

static FLUIDBOX_RAM particles_t s_pool[2];
static particles_t *s_p = &s_pool[0];
static particles_t *s_alt = &s_pool[1];

static int s_count = PARTICLE_COUNT;

static FLUIDBOX_RAM float s_density[PARTICLE_MAX];
static FLUIDBOX_RAM float s_density_near[PARTICLE_MAX];
static FLUIDBOX_RAM float s_visc_delta[PARTICLE_MAX][3];

static FLUIDBOX_RAM float s_press[PARTICLE_MAX][2];

static FLUIDBOX_RAM uint16_t s_cell_of[PARTICLE_MAX];
static FLUIDBOX_RAM uint16_t s_order[PARTICLE_MAX];
static FLUIDBOX_RAM uint16_t s_cell_start[GRID_CELLS + 1];

#define PAIR_MAX 32768
// Pair cache overflow falls back to a grid walk without dropping interactions.
static FLUIDBOX_RAM uint16_t s_pair[PAIR_MAX];
static FLUIDBOX_RAM uint32_t s_pair_off[PARTICLE_MAX + 1];
static bool s_pairs_valid;
static FLUIDBOX_RAM uint16_t s_cursor[GRID_CELLS];

static float s_rest_density;
static float s_cell_scale_x, s_cell_scale_y, s_cell_scale_z;

// Only the simulation task mutates particles; readers copy published state under the mutex.
static FLUIDBOX_RAM sim_particle_view_t s_view_pool[2][PARTICLE_MAX];
static sim_particle_view_t *s_view_work = s_view_pool[0];
static sim_particle_view_t *s_view_pub = s_view_pool[1];
static int s_view_pub_count;
static SemaphoreHandle_t s_view_lock;

static sim_stats_t s_stats;
static sim_stats_t s_stats_pub;

static uint32_t s_rng = 0x2545F491u;

static inline float rand_unit(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xFFFFFF) * (1.0f / 16777215.0f);
}

static void calibrate_rest_density(void)
{
    const float h = SMOOTH_RADIUS;
    const int reach = (int)ceilf(h / REST_SPACING);
    float rho = 0.0f;

    for (int i = -reach; i <= reach; i++) {
        for (int j = -reach; j <= reach; j++) {
            for (int k = -reach; k <= reach; k++) {
                if (i == 0 && j == 0 && k == 0) {
                    continue;
                }
                const float d = REST_SPACING * sqrtf((float)(i * i + j * j + k * k));
                if (d >= h) {
                    continue;
                }
                const float q = 1.0f - d / h;
                rho += q * q;
            }
        }
    }
    s_rest_density = rho;
}

void sim_reset(void)
{
    const float spacing = REST_SPACING;
    int nz = (int)((BOX_D - 2.0f * WALL_MARGIN) / spacing);
    if (nz < 1) nz = 1;
    const float z0 = 0.5f * (BOX_D - (float)(nz - 1) * spacing);
    const float radius = BOX_W * 0.5f - WALL_MARGIN - BOX_BACK_FILLET;
    int i = 0;
    for (float dy = radius - spacing * 0.5f; dy >= -radius && i < s_count; dy -= spacing) {
        const int nx = (int)(2.0f * sqrtf(radius * radius - dy * dy) / spacing);
        const float x0 = 0.5f * (BOX_W - (nx - 1) * spacing);
        for (int iz = 0; iz < nz && i < s_count; iz++) {
            for (int ix = 0; ix < nx && i < s_count; ix++, i++) {
                s_p->pos[i][0] = x0 + ix * spacing + (rand_unit() - 0.5f) * spacing * 0.2f;
                s_p->pos[i][1] = BOX_H * 0.5f + dy + (rand_unit() - 0.5f) * spacing * 0.2f;
                s_p->pos[i][2] = z0 + iz * spacing + (rand_unit() - 0.5f) * spacing * 0.2f;
                for (int a = 0; a < 3; a++) {
                    s_p->vel[i][a] = 0.0f;
                    s_p->old[i][a] = s_p->pos[i][a];
                }
                s_p->foam[i] = 0;
            }
        }
    }
    configASSERT(i == s_count);
    ESP_LOGI(TAG, "seeded %d particles in round case", s_count);
}

void sim_init(void)
{
    s_view_lock = xSemaphoreCreateMutex();
    configASSERT(s_view_lock);

    s_cell_scale_x = (float)GRID_CX / BOX_W;
    s_cell_scale_y = (float)GRID_CY / BOX_H;
    s_cell_scale_z = (float)GRID_CZ / BOX_D;

    const float cw = BOX_W / GRID_CX;
    const float ch = BOX_H / GRID_CY;
    const float cd = BOX_D / GRID_CZ;
    if (cw < SMOOTH_RADIUS || ch < SMOOTH_RADIUS || cd < SMOOTH_RADIUS) {
        ESP_LOGE(TAG, "grid cell %.1fx%.1fx%.1f smaller than radius %.1f",
                 (double)cw, (double)ch, (double)cd, (double)SMOOTH_RADIUS);
    }

    calibrate_rest_density();
    ESP_LOGI(TAG, "rest density %.3f (spacing %.1f, radius %.1f)",
             (double)s_rest_density, (double)REST_SPACING, (double)SMOOTH_RADIUS);

    sim_reset();
}

static void rebuild_grid(void)
{
    memset(s_cell_start, 0, sizeof(s_cell_start));

    for (int i = 0; i < s_count; i++) {
        int cx = (int)(s_p->pos[i][0] * s_cell_scale_x);
        int cy = (int)(s_p->pos[i][1] * s_cell_scale_y);
        int cz = (int)(s_p->pos[i][2] * s_cell_scale_z);
        cx = cx < 0 ? 0 : (cx >= GRID_CX ? GRID_CX - 1 : cx);
        cy = cy < 0 ? 0 : (cy >= GRID_CY ? GRID_CY - 1 : cy);
        cz = cz < 0 ? 0 : (cz >= GRID_CZ ? GRID_CZ - 1 : cz);

        const uint16_t cell = (uint16_t)CELL_INDEX(cx, cy, cz);
        s_cell_of[i] = cell;
        s_cell_start[cell + 1]++;
    }

    for (int c = 0; c < GRID_CELLS; c++) {
        s_cell_start[c + 1] = (uint16_t)(s_cell_start[c + 1] + s_cell_start[c]);
        s_cursor[c] = s_cell_start[c];
    }

    for (int i = 0; i < s_count; i++) {
        s_order[s_cursor[s_cell_of[i]]++] = (uint16_t)i;
    }

    for (int k = 0; k < s_count; k++) {
        const uint16_t src = s_order[k];
        for (int a = 0; a < 3; a++) {
            s_alt->pos[k][a] = s_p->pos[src][a];
            s_alt->vel[k][a] = s_p->vel[src][a];
            s_alt->old[k][a] = s_p->old[src][a];
        }
        s_alt->foam[k] = s_p->foam[src];
    }

    particles_t *swap = s_p;
    s_p = s_alt;
    s_alt = swap;
}

typedef struct {
    int start[9];
    int end[9];
    int runs;
} neighbourhood_t;

static void neighbourhood_for_cell(int cx, int cy, int cz, neighbourhood_t *nb)
{
    const int x0 = cx > 0 ? cx - 1 : 0;
    const int x1 = cx < GRID_CX - 1 ? cx + 1 : GRID_CX - 1;

    nb->runs = 0;
    for (int dz = -1; dz <= 1; dz++) {
        const int nz = cz + dz;
        if (nz < 0 || nz >= GRID_CZ) {
            continue;
        }
        for (int dy = -1; dy <= 1; dy++) {
            const int ny = cy + dy;
            if (ny < 0 || ny >= GRID_CY) {
                continue;
            }
            const int begin = s_cell_start[CELL_INDEX(x0, ny, nz)];
            const int stop = s_cell_start[CELL_INDEX(x1, ny, nz) + 1];
            if (stop > begin) {
                nb->start[nb->runs] = begin;
                nb->end[nb->runs] = stop;
                nb->runs++;
            }
        }
    }
}

#define FOR_EACH_CELL(cx, cy, cz, cell)                     \
    for (int cz = 0, cell = 0; cz < GRID_CZ; cz++)          \
        for (int cy = 0; cy < GRID_CY; cy++)                \
            for (int cx = 0; cx < GRID_CX; cx++, cell++)

static void compute_densities_and_viscosity(float dt)
{
    const float h2 = SMOOTH_RADIUS * SMOOTH_RADIUS;
    const float inv_h = 1.0f / SMOOTH_RADIUS;

    memset(s_density, 0, sizeof(float) * (size_t)s_count);
    memset(s_density_near, 0, sizeof(float) * (size_t)s_count);
    memset(s_visc_delta, 0, sizeof(float) * 3 * (size_t)s_count);

    uint32_t pairs = 0;
    s_pairs_valid = true;

    FOR_EACH_CELL(cx, cy, cz, cell) {
        const int i0 = s_cell_start[cell];
        const int i1 = s_cell_start[cell + 1];
        if (i0 == i1) {
            continue;
        }

        neighbourhood_t nb;
        neighbourhood_for_cell(cx, cy, cz, &nb);

        for (int i = i0; i < i1; i++) {
            const float xi = s_p->pos[i][0], yi = s_p->pos[i][1], zi = s_p->pos[i][2];
            const float vxi = s_p->vel[i][0], vyi = s_p->vel[i][1], vzi = s_p->vel[i][2];

            s_pair_off[i] = pairs;

            float rho = s_density[i];
            float rho_near = s_density_near[i];
            float dvx = 0.0f, dvy = 0.0f, dvz = 0.0f;

            for (int run = 0; run < nb.runs; run++) {
                int j = nb.start[run];
                if (j <= i) {
                    j = i + 1;  // every pair is visited exactly once
                }
                for (; j < nb.end[run]; j++) {
                    const float dx = s_p->pos[j][0] - xi;
                    const float dy = s_p->pos[j][1] - yi;
                    const float dz = s_p->pos[j][2] - zi;
                    const float r2 = dx * dx + dy * dy + dz * dz;
                    if (r2 >= h2 || r2 < 1e-6f) {
                        continue;
                    }

                    if (pairs < PAIR_MAX) {
                        s_pair[pairs++] = (uint16_t)j;
                    } else {
                        s_pairs_valid = false;
                    }

                    const float r = sqrtf(r2);
                    const float inv_r = 1.0f / r;
                    const float q = 1.0f - r * inv_h;
                    const float q2 = q * q;

                    rho += q2;
                    rho_near += q2 * q;
                    s_density[j] += q2;
                    s_density_near[j] += q2 * q;

                    const float ux = dx * inv_r, uy = dy * inv_r, uz = dz * inv_r;
                    const float u = (vxi - s_p->vel[j][0]) * ux +
                                    (vyi - s_p->vel[j][1]) * uy +
                                    (vzi - s_p->vel[j][2]) * uz;
                    if (u <= 0.0f) {
                        continue;  // Apply viscosity only to approaching particle pairs.
                    }

                    float dv = 0.5f * dt * q * (VISC_SIGMA * u + VISC_BETA * u * u);
                    if (dv > 0.5f * u) {
                        dv = 0.5f * u;
                    }
                    const float imp = dv * dt;

                    dvx -= imp * ux;
                    dvy -= imp * uy;
                    dvz -= imp * uz;
                    s_visc_delta[j][0] += imp * ux;
                    s_visc_delta[j][1] += imp * uy;
                    s_visc_delta[j][2] += imp * uz;
                }
            }

            s_density[i] = rho;
            s_density_near[i] = rho_near;
            s_visc_delta[i][0] += dvx;
            s_visc_delta[i][1] += dvy;
            s_visc_delta[i][2] += dvz;
        }
    }

    s_pair_off[s_count] = pairs;
    s_stats.pairs = (int)pairs;

    for (int i = 0; i < s_count; i++) {
        for (int a = 0; a < 3; a++) {
            s_p->pos[i][a] += s_visc_delta[i][a];
        }
    }
}

static inline void relax_pair(int i, int j, float xi, float yi, float zi,
                              float p_i, float pn_i, float dt2, float h2,
                              float inv_h, float *mx, float *my, float *mz,
                              int *clamped)
{
    const float dx = s_p->pos[j][0] - xi;
    const float dy = s_p->pos[j][1] - yi;
    const float dz = s_p->pos[j][2] - zi;
    const float r2 = dx * dx + dy * dy + dz * dz;
    if (r2 >= h2 || r2 < 1e-6f) {
        return;
    }

    const float r = sqrtf(r2);
    const float inv_r = 1.0f / r;
    const float q = 1.0f - r * inv_h;
    const float q2 = q * q;

    const float p_j = s_press[j][0];
    const float pn_j = s_press[j][1];

    float d = 0.5f * dt2 * ((p_i + p_j) * q + (pn_i + pn_j) * q2);
    if (d > MAX_DISPLACEMENT) {
        d = MAX_DISPLACEMENT;
        (*clamped)++;
    } else if (d < -MAX_DISPLACEMENT) {
        d = -MAX_DISPLACEMENT;
        (*clamped)++;
    }

    const float sx = dx * inv_r * d;
    const float sy = dy * inv_r * d;
    const float sz = dz * inv_r * d;

    s_p->pos[j][0] += sx;
    s_p->pos[j][1] += sy;
    s_p->pos[j][2] += sz;
    *mx -= sx;
    *my -= sy;
    *mz -= sz;
}

static void relax_positions(float dt)
{
    const float h2 = SMOOTH_RADIUS * SMOOTH_RADIUS;
    const float inv_h = 1.0f / SMOOTH_RADIUS;
    const float dt2 = dt * dt;

    int clamped = 0;

    for (int i = 0; i < s_count; i++) {
        s_press[i][0] = K_PRESSURE * (s_density[i] - s_rest_density);
        s_press[i][1] = K_NEAR_PRESSURE * s_density_near[i];
    }

    if (s_pairs_valid) {
        for (int i = 0; i < s_count; i++) {
            const float xi = s_p->pos[i][0], yi = s_p->pos[i][1], zi = s_p->pos[i][2];
            const float p_i = s_press[i][0];
            const float pn_i = s_press[i][1];

            float mx = 0.0f, my = 0.0f, mz = 0.0f;  // accumulated move for i

            const uint32_t k1 = s_pair_off[i + 1];
            for (uint32_t k = s_pair_off[i]; k < k1; k++) {
                relax_pair(i, s_pair[k], xi, yi, zi, p_i, pn_i, dt2, h2, inv_h,
                           &mx, &my, &mz, &clamped);
            }

            s_p->pos[i][0] += mx;
            s_p->pos[i][1] += my;
            s_p->pos[i][2] += mz;
        }
    } else {
        FOR_EACH_CELL(cx, cy, cz, cell) {
            const int i0 = s_cell_start[cell];
            const int i1 = s_cell_start[cell + 1];
            if (i0 == i1) {
                continue;
            }

            neighbourhood_t nb;
            neighbourhood_for_cell(cx, cy, cz, &nb);

            for (int i = i0; i < i1; i++) {
                const float xi = s_p->pos[i][0], yi = s_p->pos[i][1],
                            zi = s_p->pos[i][2];
                const float p_i = s_press[i][0];
                const float pn_i = s_press[i][1];

                float mx = 0.0f, my = 0.0f, mz = 0.0f;

                for (int run = 0; run < nb.runs; run++) {
                    int j = nb.start[run];
                    if (j <= i) {
                        j = i + 1;
                    }
                    for (; j < nb.end[run]; j++) {
                        relax_pair(i, j, xi, yi, zi, p_i, pn_i, dt2, h2, inv_h,
                                   &mx, &my, &mz, &clamped);
                    }
                }

                s_p->pos[i][0] += mx;
                s_p->pos[i][1] += my;
                s_p->pos[i][2] += mz;
            }
        }
    }

    s_stats.clamped = clamped;
}

// Project positions inside the rounded enclosure and its front/back fillets.
// Apply restitution and tangential damping without deriving velocity from the projection.
static void resolve_walls(void)
{
    const float lo_z = WALL_MARGIN;
    const float hi_z = BOX_D - WALL_MARGIN;

    const float arc_lo_x = BOX_CORNER_R;
    const float arc_hi_x = BOX_W - BOX_CORNER_R;
    const float arc_lo_y = BOX_CORNER_R;
    const float arc_hi_y = BOX_H - BOX_CORNER_R;

    const float side_r = BOX_CORNER_R - WALL_MARGIN;

    const float front_f = BOX_FRONT_FILLET;
    const float back_f = BOX_BACK_FILLET;

    for (int i = 0; i < s_count; i++) {
        s_p->foam[i] = (uint8_t)(s_p->foam[i] * FOAM_FADE >> 8);
        const float x = s_p->pos[i][0];
        const float y = s_p->pos[i][1];
        const float z = s_p->pos[i][2];

        const float ax = x < arc_lo_x ? arc_lo_x : (x > arc_hi_x ? arc_hi_x : x);
        const float ay = y < arc_lo_y ? arc_lo_y : (y > arc_hi_y ? arc_hi_y : y);
        float ux = x - ax;
        float uy = y - ay;
        float r = sqrtf(ux * ux + uy * uy);
        if (r > 1e-6f) {
            ux /= r;
            uy /= r;
        } else {
            ux = 0.0f;
            uy = 0.0f;
        }

        float fillet, cz;
        if (z < lo_z + front_f) {
            fillet = front_f;
            cz = lo_z + front_f;
        } else if (z > hi_z - back_f) {
            fillet = back_f;
            cz = hi_z - back_f;
        } else {
            fillet = 0.0f;
            cz = z;
        }

        const float lim = side_r - fillet;
        const float cr = r < lim ? r : lim;
        const float dr = r - cr;
        const float dz = z - cz;
        const float dd2 = dr * dr + dz * dz;

        if (dd2 <= fillet * fillet) {
            continue;
        }

        const float dd = sqrtf(dd2);
        const float nr = dr / dd;
        const float nz = dz / dd;

        const float inset = fillet - WALL_JITTER * rand_unit();

        s_p->pos[i][0] = ax + ux * (cr + nr * inset);
        s_p->pos[i][1] = ay + uy * (cr + nr * inset);
        s_p->pos[i][2] = cz + nz * inset;

        const float nx = ux * nr;
        const float ny = uy * nr;

        const float vn = s_p->vel[i][0] * nx + s_p->vel[i][1] * ny + s_p->vel[i][2] * nz;
        // Generate particle-carried foam from screen-plane impacts above the speed
        // threshold. Exclude the z component to avoid foam from front/back contact alone.
        const float hit = (s_p->vel[i][0] * nx + s_p->vel[i][1] * ny - FOAM_HIT_SPEED) * (255.0f / FOAM_HIT_RANGE);
        if (hit > s_p->foam[i]) s_p->foam[i] = (uint8_t)(hit < 255.0f ? hit : 255.0f);
        if (vn > 0.0f) {
            const float k = (1.0f + WALL_RESTITUTION) * vn;
            s_p->vel[i][0] -= k * nx;
            s_p->vel[i][1] -= k * ny;
            s_p->vel[i][2] -= k * nz;
        }

        const float keep =
            s_p->vel[i][0] * nx + s_p->vel[i][1] * ny + s_p->vel[i][2] * nz;
        const float restore = keep * (1.0f - WALL_FRICTION);
        s_p->vel[i][0] = s_p->vel[i][0] * WALL_FRICTION + restore * nx;
        s_p->vel[i][1] = s_p->vel[i][1] * WALL_FRICTION + restore * ny;
        s_p->vel[i][2] = s_p->vel[i][2] * WALL_FRICTION + restore * nz;
    }
}

static void integrate_velocities(float dt, const sim_forces_t *f)
{
    const float gx = f->gravity[0], gy = f->gravity[1], gz = f->gravity[2];
    const float wx = f->omega[0], wy = f->omega[1], wz = f->omega[2];
    const float ax = f->alpha[0], ay = f->alpha[1], az = f->alpha[2];

    const float w2 = wx * wx + wy * wy + wz * wz;
    const bool rotating = (ROTATION_GAIN != 0.0f) &&
                          (w2 > 1e-8f || (ax * ax + ay * ay + az * az) > 1e-8f);

    const float cx = BOX_W * 0.5f, cy = BOX_H * 0.5f, cz = BOX_D * 0.5f;

    for (int i = 0; i < s_count; i++) {
        float accx = gx, accy = gy, accz = gz;

        if (rotating) {
            const float rx = s_p->pos[i][0] - cx;
            const float ry = s_p->pos[i][1] - cy;
            const float rz = s_p->pos[i][2] - cz;

            const float wdotr = wx * rx + wy * ry + wz * rz;
            accx += ROTATION_GAIN * (rx * w2 - wx * wdotr);
            accy += ROTATION_GAIN * (ry * w2 - wy * wdotr);
            accz += ROTATION_GAIN * (rz * w2 - wz * wdotr);

            accx -= ROTATION_GAIN * (ay * rz - az * ry);
            accy -= ROTATION_GAIN * (az * rx - ax * rz);
            accz -= ROTATION_GAIN * (ax * ry - ay * rx);

            const float vx = s_p->vel[i][0], vy = s_p->vel[i][1], vz = s_p->vel[i][2];
            accx -= ROTATION_GAIN * 2.0f * (wy * vz - wz * vy);
            accy -= ROTATION_GAIN * 2.0f * (wz * vx - wx * vz);
            accz -= ROTATION_GAIN * 2.0f * (wx * vy - wy * vx);
        }

        s_p->vel[i][0] += accx * dt;
        s_p->vel[i][1] += accy * dt;
        s_p->vel[i][2] += accz * dt;
    }
}

static void substep(float dt, const sim_forces_t *f)
{
    integrate_velocities(dt, f);

    const float inv_dt = 1.0f / dt;

    for (int i = 0; i < s_count; i++) {
        for (int a = 0; a < 3; a++) {
            s_p->old[i][a] = s_p->pos[i][a];
            s_p->pos[i][a] += s_p->vel[i][a] * dt;
        }
    }

    int64_t t0 = esp_timer_get_time();
    rebuild_grid();
    s_stats.us_grid = (int)(esp_timer_get_time() - t0);

    t0 = esp_timer_get_time();
    compute_densities_and_viscosity(dt);
    s_stats.us_density = (int)(esp_timer_get_time() - t0);

    t0 = esp_timer_get_time();
    relax_positions(dt);
    s_stats.us_relax = (int)(esp_timer_get_time() - t0);

    for (int i = 0; i < s_count; i++) {
        for (int a = 0; a < 3; a++) {
            s_p->vel[i][a] = (s_p->pos[i][a] - s_p->old[i][a]) * inv_dt;
        }
    }

    resolve_walls();
}

static void publish(void)
{
    float sum_rho = 0.0f, sum_speed = 0.0f, max_speed = 0.0f;

    for (int i = 0; i < s_count; i++) {
        const float vx = s_p->vel[i][0], vy = s_p->vel[i][1], vz = s_p->vel[i][2];
        const float speed = sqrtf(vx * vx + vy * vy + vz * vz);

        s_view_work[i].x = s_p->pos[i][0];
        s_view_work[i].y = s_p->pos[i][1];
        s_view_work[i].z = s_p->pos[i][2];
        s_view_work[i].speed = speed;
        s_view_work[i].foam = s_p->foam[i];

        sum_rho += s_density[i];
        sum_speed += speed;
        if (speed > max_speed) {
            max_speed = speed;
        }
    }

    const float inv_n = 1.0f / (float)s_count;
    s_stats.mean_density = sum_rho * inv_n;
    s_stats.rest_density = s_rest_density;
    s_stats.mean_speed = sum_speed * inv_n;
    s_stats.max_speed = max_speed;

    xSemaphoreTake(s_view_lock, portMAX_DELAY);
    sim_particle_view_t *swap = s_view_pub;
    s_view_pub = s_view_work;
    s_view_work = swap;
    s_view_pub_count = s_count;
    s_stats_pub = s_stats;
    xSemaphoreGive(s_view_lock);
}

// Limit scaled simulation time per substep to reduce pressure-relaxation instability.
void sim_step(float dt_real, const sim_forces_t *forces)
{
    float dt = (dt_real * TIME_SCALE) / (float)SUBSTEPS;
    if (dt > SIM_DT_MAX) {
        dt = SIM_DT_MAX;
    }

    sim_forces_t scaled = *forces;
    for (int a = 0; a < 3; a++) {
        scaled.omega[a] *= TIME_SCALE;
        scaled.alpha[a] *= TIME_SCALE * TIME_SCALE;
    }

    for (int s = 0; s < SUBSTEPS; s++) {
        substep(dt, &scaled);
    }
    publish();
}

int sim_snapshot(sim_particle_view_t *out, int max)
{
    xSemaphoreTake(s_view_lock, portMAX_DELAY);
    int n = s_view_pub_count;
    if (n > max) {
        n = max;
    }
    if (n > 0) {
        memcpy(out, s_view_pub, (size_t)n * sizeof(sim_particle_view_t));
    }
    xSemaphoreGive(s_view_lock);
    return n;
}

void sim_stats(sim_stats_t *out)
{
    xSemaphoreTake(s_view_lock, portMAX_DELAY);
    *out = s_stats_pub;
    xSemaphoreGive(s_view_lock);
}
