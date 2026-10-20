#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "config.h"
#include "display.h"
#include "esp_timer.h"

// LVGL takes native RGB565; the existing LCD adapter handles wire byte order.
#define SWAP16(v) ((uint16_t)(v))

#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)
_Static_assert(16 * BAND_ROWS * 2 % 64 == 0, "16 px band windows must span whole 64-byte cache lines");

// Match frame-slot alignment to permit word-aligned staging copies.
static FLUIDBOX_RAM uint16_t s_band[BAND_PIXELS] __attribute__((aligned(64)));
static FLUIDBOX_RAM uint16_t s_color_lut[DEPTH_LEVELS * SPEED_LEVELS];
static FLUIDBOX_RAM uint16_t s_highlight_lut[DEPTH_LEVELS * SPEED_LEVELS];

static FLUIDBOX_RAM uint8_t s_disc_span[DISC_MAX_R + 1][2 * DISC_MAX_R + 1];

// A previously occupied band must be cleared once before it can be skipped.
typedef struct { int left, right; } band_extent_t;
static FLUIDBOX_RAM band_extent_t s_extent[BAND_COUNT], s_previous[BAND_COUNT];

static FLUIDBOX_RAM sim_particle_view_t s_snapshot[PARTICLE_MAX];
static FLUIDBOX_RAM int16_t s_sx[PARTICLE_MAX];
static FLUIDBOX_RAM int16_t s_sy[PARTICLE_MAX];
static FLUIDBOX_RAM uint8_t s_sr[PARTICLE_MAX];
static FLUIDBOX_RAM uint16_t s_sc[PARTICLE_MAX];
static FLUIDBOX_RAM uint16_t s_sh[PARTICLE_MAX];

#define WATER_W (LCD_H_RES / WATER_GRID_STEP + 1)
#define WATER_H (LCD_V_RES / WATER_GRID_STEP + 1)
_Static_assert(LCD_H_RES % WATER_GRID_STEP == 0 && LCD_V_RES % WATER_GRID_STEP == 0,
               "Water grid must cover the complete panel");
static FLUIDBOX_RAM uint16_t s_water[WATER_H][WATER_W];  // binned and blurred particle density
static FLUIDBOX_RAM uint16_t s_speed[WATER_H][WATER_W];  // Weighted field; 64 * s_speed / s_water estimates speed / 32, or foam in liquid mode.
static FLUIDBOX_RAM uint8_t s_tone[WATER_H][WATER_W];    // Chamfer-distance scratch, then the mode-specific palette index.
static FLUIDBOX_RAM uint8_t s_depth_tone[256];           // chamfer depth below the surface -> palette index
static FLUIDBOX_RAM uint16_t s_water_lut[DITHER_PHASES][256];  // palette in ordered-dither phases, see dither_phase()
static FLUIDBOX_RAM uint16_t s_poster_lut[256];          // Quantized liquid palette, with pale tones for foam.
static FLUIDBOX_RAM uint16_t s_edge_lut[WATER_EDGE_STEPS];
static FLUIDBOX_RAM uint8_t s_caustic[64][64];           // tileable light network, drifted in two layers
static FLUIDBOX_RAM uint16_t s_light[511];               // caustic brightness by summed layers, faded while moving
static int s_caustic_gain = 256;            // 0..256, eased toward calm-water strength each frame
static FLUIDBOX_RAM uint16_t s_ukiyo_lut[256];           // flat woodblock colours by depth below the foam line
static FLUIDBOX_RAM uint16_t s_paper[2][LCD_V_RES];      // Dithered paper background, darker at the top.
static uint16_t s_ink_soft;                 // antialiasing step between paper and ink
static FLUIDBOX_RAM uint8_t s_foam_tex[64][64];          // scalloped foam lobes and spray dots, in px of extra foam
#define MOTTLE_W (LCD_H_RES / MOTTLE_PX + 2)
#define MOTTLE_H (LCD_V_RES / MOTTLE_PX + 2)
static FLUIDBOX_RAM uint8_t s_mottle[MOTTLE_H][MOTTLE_W];  // random light strength, bilinear between samples
static FLUIDBOX_RAM uint8_t s_foam_amp[LCD_H_RES];         // random foam scale along the crest
static FLUIDBOX_RAM uint16_t s_lava_lut[DITHER_PHASES][256];   // lava heat palette in the same dither phases
static FLUIDBOX_RAM uint16_t s_lava_edge[WATER_EDGE_STEPS];
static FLUIDBOX_RAM uint8_t s_lava_depth[256];           // chamfer depth -> heat: cooled crust on top, molten core below
static atomic_int s_mode_requested;
static _Atomic float s_gravity[3] = {0.0f, 1.0f, 0.0f};
static float s_up[3] = {0.0f, -1.0f, 0.0f};  // Smoothed lighting direction: x right, y down, z toward the viewer.

void render_set_mode(render_mode_t mode) { atomic_store(&s_mode_requested, mode); }

void render_set_gravity(const float gravity[3])
{
    for (int i = 0; i < 3; ++i) atomic_store(&s_gravity[i], gravity[i]);
}

static void normalize3(float *v)
{
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) for (int i = 0; i < 3; ++i) v[i] /= len;
}

// Smooth the direction opposite the gravity input, reversing z for view space.
static void update_up(void)
{
    float target[3] = {-atomic_load(&s_gravity[0]), -atomic_load(&s_gravity[1]), atomic_load(&s_gravity[2])};
    normalize3(target);
    for (int i = 0; i < 3; ++i) s_up[i] += (target[i] - s_up[i]) * 0.2f;
    normalize3(s_up);
}

static inline uint16_t rgb565(int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#define RAMP_STOPS 4

static void speed_color(float t, int *r, int *g, int *b)
{
    static const float stop_t[RAMP_STOPS] = {0.00f, 0.45f, 0.78f, 1.00f};
    static const int stop_c[RAMP_STOPS][3] = {
        {10, 45, 165},
        {40, 125, 235},
        {150, 205, 250},
        {255, 255, 255},
    };

    if (t <= 0.0f) {
        *r = stop_c[0][0]; *g = stop_c[0][1]; *b = stop_c[0][2];
        return;
    }
    if (t >= 1.0f) {
        *r = stop_c[RAMP_STOPS - 1][0];
        *g = stop_c[RAMP_STOPS - 1][1];
        *b = stop_c[RAMP_STOPS - 1][2];
        return;
    }

    for (int s = 0; s < RAMP_STOPS - 1; s++) {
        if (t <= stop_t[s + 1]) {
            const float f = (t - stop_t[s]) / (stop_t[s + 1] - stop_t[s]);
            *r = (int)(stop_c[s][0] + f * (stop_c[s + 1][0] - stop_c[s][0]));
            *g = (int)(stop_c[s][1] + f * (stop_c[s + 1][1] - stop_c[s][1]));
            *b = (int)(stop_c[s][2] + f * (stop_c[s + 1][2] - stop_c[s][2]));
            return;
        }
    }
}

// Water palette: deep navy, shallow aqua and pale foam.
static void water_color(float t, float *rgb)
{
    static const float stop_t[] = {0.00f, 0.30f, 0.55f, 0.72f, 0.84f, 0.93f, 1.00f};
    static const int stop_c[][3] = {
        {2, 10, 28}, {4, 38, 92}, {8, 88, 150}, {16, 140, 188},
        {40, 196, 214}, {130, 236, 236}, {235, 255, 252},
    };
    int s = 0;
    while (s < 5 && t > stop_t[s + 1]) s++;
    float f = (t - stop_t[s]) / (stop_t[s + 1] - stop_t[s]);
    if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
    for (int c = 0; c < 3; c++) rgb[c] = stop_c[s][c] + f * (stop_c[s + 1][c] - stop_c[s][c]);
}

// Black crust, deep red, orange, amber, then yellow-white where the lava is hottest.
static void lava_color(float t, float *rgb)
{
    static const float stop_t[] = {0.00f, 0.18f, 0.35f, 0.55f, 0.72f, 0.86f, 0.95f, 1.00f};
    static const int stop_c[][3] = {
        {8, 1, 0}, {45, 5, 2}, {110, 12, 2}, {200, 40, 4},
        {250, 100, 10}, {255, 170, 40}, {255, 225, 120}, {255, 250, 220},
    };
    int s = 0;
    while (s < 6 && t > stop_t[s + 1]) s++;
    float f = (t - stop_t[s]) / (stop_t[s + 1] - stop_t[s]);
    if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
    for (int c = 0; c < 3; c++) rgb[c] = stop_c[s][c] + f * (stop_c[s + 1][c] - stop_c[s][c]);
}

// Ordered dithering reduces RGB565 banding. Phase increments of 3 horizontally
// and 5 vertically cover all eight thresholds in each eight-pixel run, giving
// rows and columns equal average thresholds to reduce directional striping.
static void build_dithered(void (*color)(float, float *), uint16_t lut[DITHER_PHASES][256])
{
    for (int i = 0; i < 256; ++i) {
        float rgb[3];
        color(i / 255.0f, rgb);
        for (int p = 0; p < DITHER_PHASES; ++p) {
            const float k = (p + 0.5f) / DITHER_PHASES;
            const int r = (int)(rgb[0] * 31 / 255 + k), g = (int)(rgb[1] * 63 / 255 + k);
            const int b = (int)(rgb[2] * 31 / 255 + k);
            lut[p][i] = SWAP16((uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b)));
        }
    }
}

static inline int dither_phase(int x, int row_phase) { return (3 * x + row_phase) & (DITHER_PHASES - 1); }

static void build_edge(void (*color)(float, float *), int tone, uint16_t *edge)
{
    float rgb[3];
    color(tone / 255.0f, rgb);
    for (int k = 0; k < WATER_EDGE_STEPS; ++k) {
        const float a = (k + 0.5f) / WATER_EDGE_STEPS;
        edge[k] = SWAP16(rgb565((int)(rgb[0] * a), (int)(rgb[1] * a), (int)(rgb[2] * a)));
    }
}

static void build_water_tables(void)
{
    build_dithered(water_color, s_water_lut);
    static const struct { int from; uint8_t rgb[3]; } bands[] = {
        {0, {3, 18, 48}}, {64, {6, 48, 108}}, {112, {12, 92, 160}}, {156, {26, 146, 200}},
        {196, {84, 204, 226}}, {232, {214, 246, 246}},
    };
    for (int t = 0, k = 0; t < 256; ++t) {
        if (k + 1 < (int)(sizeof(bands) / sizeof(bands[0])) && t >= bands[k + 1].from) ++k;
        s_poster_lut[t] = SWAP16(rgb565(bands[k].rgb[0], bands[k].rgb[1], bands[k].rgb[2]));
    }
    build_edge(water_color, WATER_RIM_TONE, s_edge_lut);
    build_dithered(lava_color, s_lava_lut);
    build_edge(lava_color, LAVA_RIM_TONE, s_lava_edge);
    // Chamfer units are a third of a grid cell.
    for (int d = 0; d < 256; ++d) {
        const float px = d * WATER_GRID_STEP / 3.0f;
        s_depth_tone[d] = (uint8_t)(WATER_DEEP_TONE + (WATER_SURFACE_TONE - WATER_DEEP_TONE) * expf(-px / WATER_ABSORB_PX));
        s_lava_depth[d] = (uint8_t)(LAVA_CORE_TONE + (LAVA_CRUST_TONE - LAVA_CORE_TONE) * expf(-px / LAVA_CRUST_PX));
    }
    // Woodblock bands by px below the foam line: ink line, pale blue, mid blue with stripes, Prussian blue.
    for (int v = 0; v < 256; ++v) {
        const int d = v - UKIYO_FOAM_LINE;
        static const int foam[3] = {246, 240, 222}, ink[3] = {22, 38, 66}, pale[3] = {134, 166, 190},
                         mid[3] = {58, 98, 142}, prussian[3] = {28, 62, 110};
        const int *c = d < 0 ? foam : d < 2 ? ink : d < 15 ? pale : d < 40 ? mid : prussian;
        // Pale stripes at selected depths produce lines parallel to the surface.
        if ((d >= 22 && d < 24) || (d >= 31 && d < 33) || (d >= 44 && (d - 44) % 15 < 2)) c = pale;
        s_ukiyo_lut[v] = SWAP16(rgb565(c[0], c[1], c[2]));
    }
    for (int y = 0; y < LCD_V_RES; ++y) {
        const float t = (float)y / (LCD_V_RES - 1);
        for (int p = 0; p < 2; ++p) {  // two dither phases per row, alternated by column and row
            const float k = ((y + p) & 1) ? 0.75f : 0.25f;
            const int r = (int)((198 + 40 * t) * 31 / 255 + k), g = (int)((186 + 43 * t) * 63 / 255 + k);
            const int b = (int)((156 + 48 * t) * 31 / 255 + k);
            s_paper[p][y] = SWAP16((uint16_t)((r << 11) | (g << 5) | b));
        }
    }
    s_ink_soft = SWAP16(rgb565(120, 116, 112));
    // Union of discs around Worley points gives a cloud-like scalloped foam edge; a sparse second
    // set of tiny discs reaches deeper and leaves spray dots in the pale band.
    int lx[UKIYO_LOBES], ly[UKIYO_LOBES], dx_[UKIYO_DOTS], dy_[UKIYO_DOTS];
    uint32_t lseed = 0x2545F491u;
    for (int i = 0; i < UKIYO_LOBES; ++i) {
        lseed = lseed * 1664525u + 1013904223u; lx[i] = lseed >> 26;
        lseed = lseed * 1664525u + 1013904223u; ly[i] = lseed >> 26;
    }
    for (int i = 0; i < UKIYO_DOTS; ++i) {
        lseed = lseed * 1664525u + 1013904223u; dx_[i] = lseed >> 26;
        lseed = lseed * 1664525u + 1013904223u; dy_[i] = lseed >> 26;
    }
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        float lobe = 0.0f, dot = 0.0f;
        for (int i = 0; i < UKIYO_LOBES + UKIYO_DOTS; ++i) {
            const bool is_dot = i >= UKIYO_LOBES;
            int ddx = abs(x - (is_dot ? dx_[i - UKIYO_LOBES] : lx[i])), ddy = abs(y - (is_dot ? dy_[i - UKIYO_LOBES] : ly[i]));
            if (ddx > 32) ddx = 64 - ddx;
            if (ddy > 32) ddy = 64 - ddy;
            const float r2 = (float)(ddx * ddx + ddy * ddy);
            if (is_dot) { if (r2 < 5.0f) dot = UKIYO_DOT_DEPTH; }
            else if (r2 < UKIYO_LOBE_R * UKIYO_LOBE_R) {
                const float h = UKIYO_LOBE_PX * sqrtf(1.0f - r2 / (UKIYO_LOBE_R * UKIYO_LOBE_R));
                if (h > lobe) lobe = h;
            }
        }
        s_foam_tex[y][x] = (uint8_t)(dot > lobe ? dot : lobe);
    }
    // Small differences between the two nearest feature distances form bright caustic lines.
    int px[WATER_CAUSTIC_CELLS], py[WATER_CAUSTIC_CELLS];
    uint32_t seed = 0x9E3779B9u;
    for (int i = 0; i < WATER_CAUSTIC_CELLS; ++i) {
        seed = seed * 1664525u + 1013904223u; px[i] = seed >> 26;
        seed = seed * 1664525u + 1013904223u; py[i] = seed >> 26;
    }
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        // A periodic warp bends the straight cell borders into caustic curves and keeps the tile seamless.
        const float wx = x + 3.0f * sinf(y * (float)M_PI / 16.0f), wy = y + 3.0f * sinf(x * (float)M_PI / 32.0f);
        float f1 = 1e9f, f2 = 1e9f;
        for (int i = 0; i < WATER_CAUSTIC_CELLS; ++i) {
            float dx = fabsf(wx - px[i]), dy = fabsf(wy - py[i]);
            dx = fmodf(dx, 64.0f); dy = fmodf(dy, 64.0f);
            if (dx > 32.0f) dx = 64.0f - dx;
            if (dy > 32.0f) dy = 64.0f - dy;
            const float d = sqrtf(dx * dx + dy * dy);
            if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
        }
        float v = 1.0f - (f2 - f1) / 5.0f;
        v = v > 0.0f ? v * v : 0.0f;
        s_caustic[y][x] = (uint8_t)(v * 255.0f);
    }
    // Squaring the random samples leaves mostly dim patches with a few bright ones.
    for (int y = 0; y < MOTTLE_H; ++y) for (int x = 0; x < MOTTLE_W; ++x) {
        seed = seed * 1664525u + 1013904223u;
        const int r = seed >> 24;
        s_mottle[y][x] = (uint8_t)(MOTTLE_MIN + (255 - MOTTLE_MIN) * r * r / (255 * 255));
    }
    // Smoothly interpolate random foam scales to vary lobe size along the crest.
    int a = 0, b = 0;
    for (int x = 0; x < LCD_H_RES; ++x) {
        if (x % UKIYO_FOAM_VARY_PX == 0) {
            if (x == 0) { lseed = lseed * 1664525u + 1013904223u; b = lseed >> 24; }
            a = b;
            lseed = lseed * 1664525u + 1013904223u; b = lseed >> 24;
        }
        const float t = (float)(x % UKIYO_FOAM_VARY_PX) / UKIYO_FOAM_VARY_PX, s = t * t * (3.0f - 2.0f * t);
        s_foam_amp[x] = (uint8_t)(UKIYO_FOAM_MIN + (255 - UKIYO_FOAM_MIN) * (a + (b - a) * s) / 255);
    }
}

static void build_color_lut(void)
{
    for (int d = 0; d < DEPTH_LEVELS; d++) {
        const float depth_t = (DEPTH_LEVELS > 1) ? (float)d / (float)(DEPTH_LEVELS - 1) : 0.0f;
        const float dim = DEPTH_DIM_MIN + (1.0f - DEPTH_DIM_MIN) * (1.0f - depth_t);

        for (int s = 0; s < SPEED_LEVELS; s++) {
            const float linear =
                (SPEED_LEVELS > 1) ? (float)s / (float)(SPEED_LEVELS - 1) : 0.0f;
            const float speed_t = powf(linear, SPEED_COLOR_GAMMA);

            int r = 0, g = 0, b = 0;
            speed_color(speed_t, &r, &g, &b);
            s_color_lut[d * SPEED_LEVELS + s] =
                SWAP16(rgb565((int)(r * dim), (int)(g * dim), (int)(b * dim)));

            const float lift = HIGHLIGHT_LIFT;
            const int hr = (int)((r + (255 - r) * lift) * dim);
            const int hg = (int)((g + (255 - g) * lift) * dim);
            const int hb = (int)((b + (255 - b) * lift) * dim);
            s_highlight_lut[d * SPEED_LEVELS + s] = SWAP16(rgb565(hr, hg, hb));
        }
    }
}

static void build_disc_spans(void)
{
    for (int r = 0; r <= DISC_MAX_R; r++) {
        for (int dy = -r; dy <= r; dy++) {
            const float w = sqrtf((float)(r * r - dy * dy));
            s_disc_span[r][dy + r] = (uint8_t)(w + 0.5f);
        }
    }
}

static inline void project(float x, float y, float z, int *out_x, int *out_y, float *out_scale)
{
    const float s = PROJ_FOCAL / (PROJ_FOCAL + z);
    *out_x = (int)((BOX_W * 0.5f) + (x - BOX_W * 0.5f) * s + 0.5f);
    *out_y = (int)((BOX_H * 0.5f) + (y - BOX_H * 0.5f) * s + 0.5f);
    *out_scale = s;
}

void render_init(void)
{
    build_color_lut();
    build_disc_spans();
    build_water_tables();

    // Panel RAM is undefined at startup; transmit every band on the first frame.
    for (int b = 0; b < BAND_COUNT; b++) {
        s_previous[b] = (band_extent_t){0, LCD_H_RES};
    }
}

static inline void draw_disc(uint16_t *buf, int band_y0, int left, int width, int cx, int cy, int r, uint16_t color)
{
    if (r < 1) r = 1;
    if (r > DISC_MAX_R) r = DISC_MAX_R;

    cx -= left;
    const uint8_t *spans = s_disc_span[r];

    int dy0 = -r;
    int dy1 = r;
    if (cy + dy0 < band_y0) dy0 = band_y0 - cy;
    if (cy + dy1 >= band_y0 + BAND_ROWS) dy1 = band_y0 + BAND_ROWS - 1 - cy;

    for (int dy = dy0; dy <= dy1; dy++) {
        const int hw = spans[dy + r];
        int x0 = cx - hw;
        int x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= width) x1 = width - 1;
        if (x0 > x1) {
            continue;
        }

        uint16_t *row = buf + (cy + dy - band_y0) * width;
        for (int x = x0; x <= x1; x++) {
            row[x] = color;
        }
    }
}

static void project_all(int n, bool flat_tank)
{
    const float speed_scale = (float)(SPEED_LEVELS - 1) / SPEED_COLOR_MAX;
    const float depth_scale = (float)(DEPTH_LEVELS - 1) / BOX_D;

    for (int b = 0; b < BAND_COUNT; ++b) s_extent[b] = (band_extent_t){LCD_H_RES, 0};

    for (int i = 0; i < n; i++) {
        int sx, sy;
        float scale;
        // Surface modes omit perspective scaling so all depths share the same screen boundary.
        if (flat_tank) project(s_snapshot[i].x, s_snapshot[i].y, 0.0f, &sx, &sy, &scale);
        else project(s_snapshot[i].x, s_snapshot[i].y, s_snapshot[i].z, &sx, &sy, &scale);

        s_sx[i] = (int16_t)sx;
        s_sy[i] = (int16_t)sy;

        int r = (int)(PARTICLE_RADIUS_PX * scale + 0.5f);
        if (r < 1) r = 1;
        if (r > DISC_MAX_R) r = DISC_MAX_R;
        s_sr[i] = (uint8_t)r;

        int sl = (int)(s_snapshot[i].speed * speed_scale);
        if (sl < 0) sl = 0;
        if (sl > SPEED_LEVELS - 1) sl = SPEED_LEVELS - 1;

        int dl = (int)(s_snapshot[i].z * depth_scale + 0.5f);
        if (dl < 0) dl = 0;
        if (dl > DEPTH_LEVELS - 1) dl = DEPTH_LEVELS - 1;

        const int lut = dl * SPEED_LEVELS + sl;
        s_sc[i] = s_color_lut[lut];
        s_sh[i] = s_highlight_lut[lut];

        const int extent_r = flat_tank ? WATER_RADIUS_PX + WATER_GRID_STEP : r;
        const int top = sy - extent_r;
        const int bot = sy + extent_r;
        if (bot < 0 || top >= LCD_V_RES) {
            continue;
        }
        const int b0 = (top < 0) ? 0 : top / BAND_ROWS;
        const int b1 = (bot >= LCD_V_RES) ? (BAND_COUNT - 1) : bot / BAND_ROWS;
        for (int b = b0; b <= b1; b++) {
            if (sx - extent_r < s_extent[b].left) s_extent[b].left = sx - extent_r;
            if (sx + extent_r + 1 > s_extent[b].right) s_extent[b].right = sx + extent_r + 1;
        }
    }
}

// Five-cell box filter with mirrored samples at the field boundaries.
static void box5(uint16_t *p, int n, int stride)
{
    int v[WATER_W + 4];
    for (int i = -2; i < n + 2; ++i) v[i + 2] = p[(i < 0 ? -i : i >= n ? 2 * (n - 1) - i : i) * stride];
    int sum = v[0] + v[1] + v[2] + v[3];
    for (int i = 0; i < n; ++i) {
        sum += v[i + 4];
        p[i * stride] = (uint16_t)(sum / 5);
        sum -= v[i];
    }
}

static void blur(uint16_t (*f)[WATER_W])
{
    // Two box passes per axis reach four cells; with bilinear binning that is WATER_RADIUS_PX.
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < WATER_H; ++y) box5(f[y], WATER_W, 1);
        for (int x = 0; x < WATER_W; ++x) box5(&f[0][x], WATER_H, WATER_W);
    }
}

static inline void add16(uint16_t *p, int v) { v += *p; *p = (uint16_t)(v < 65535 ? v : 65535); }

static inline int min_i(int a, int b) { return a < b ? a : b; }

// Exclude the corner mask from free-surface seeds in the chamfer-distance field.
static bool outside_case(int gx, int gy)
{
    const float dx = fabsf(gx * WATER_GRID_STEP - BOX_W * 0.5f) - (BOX_W * 0.5f - BOX_CORNER_R);
    const float dy = fabsf(gy * WATER_GRID_STEP - BOX_H * 0.5f) - (BOX_H * 0.5f - BOX_CORNER_R);
    return dx > 0.0f && dy > 0.0f && dx * dx + dy * dy > BOX_CORNER_R * BOX_CORNER_R * 0.5f;
}

static void build_water(int n, render_mode_t mode)
{
    enum { S = WATER_GRID_STEP, BIN = 28 };  // Bilinear density weights sum to S * S * BIN per particle.
    _Static_assert(WATER_W == WATER_H, "box5 scratch is sized by width");
    // Bin density before speed so gap filling can reuse s_speed as scratch,
    // avoiding an additional full-grid allocation.
    for (int pass = 0; pass < 2; ++pass) {
        uint16_t (*field)[WATER_W] = pass ? s_speed : s_water;
        memset(field, 0, sizeof(s_water));
        for (int i = 0; i < n; ++i) {
            const int x = s_sx[i] < 0 ? 0 : s_sx[i] >= LCD_H_RES ? LCD_H_RES - 1 : s_sx[i];
            const int y = s_sy[i] < 0 ? 0 : s_sy[i] >= LCD_V_RES ? LCD_V_RES - 1 : s_sy[i];
            const int gx = x / S, gy = y / S, fx = x % S, fy = y % S;
            int speed = mode == RENDER_LIQUID ? s_snapshot[i].foam : s_snapshot[i].speed / 32.0f;
            if (speed > 255) speed = 255;
            const int w[4] = {(S - fx) * (S - fy) * BIN, fx * (S - fy) * BIN, (S - fx) * fy * BIN, fx * fy * BIN};
            for (int k = 0; k < 4; ++k) add16(&field[gy + (k >> 1)][gx + (k & 1)], pass ? w[k] * speed >> 6 : w[k]);
        }
        blur(field);
        // A wider blurred field raises density in interior gaps. Ukiyo-e omits
        // this fill because near-threshold regions produce dark ink outlines;
        // retaining the paper background preserves its foam appearance.
        if (pass == 0 && mode != RENDER_UKIYOE) {
            memcpy(s_speed, s_water, sizeof(s_speed));
            blur(s_speed);
            blur(s_speed);
            for (int y = 0; y < WATER_H; ++y) for (int x = 0; x < WATER_W; ++x) {
                const int wide = s_speed[y][x] * WATER_FILL_GAIN / 4 - WATER_FILL_BIAS;
                if (wide > s_water[y][x]) s_water[y][x] = (uint16_t)(wide < 65535 ? wide : 65535);
            }
        }
    }
    // The wide blur reaches past the particle extents, so bands must cover the field itself.
    // Surface coverage requires at least one surrounding grid node to reach the edge threshold.
    for (int y = 0; y < WATER_H; ++y) {
        int x0 = -1, x1 = -1;
        for (int x = 0; x < WATER_W; ++x) {
            if (s_water[y][x] < WATER_THRESHOLD - WATER_EDGE) continue;
            if (x0 < 0) x0 = x;
            x1 = x;
        }
        if (x0 < 0) continue;
        const int top = (y - 1) * S < 0 ? 0 : (y - 1) * S, bot = (y + 1) * S < LCD_V_RES ? (y + 1) * S : LCD_V_RES;
        for (int b = top / BAND_ROWS; b <= (bot - 1) / BAND_ROWS; ++b) {
            if ((x0 - 1) * S < s_extent[b].left) s_extent[b].left = (x0 - 1) * S;
            if ((x1 + 1) * S > s_extent[b].right) s_extent[b].right = (x1 + 1) * S;
        }
    }

    if (mode == RENDER_LIQUID) return;
    // Two-pass 3-4 chamfer distance to the free surface; off-grid cells count as water.
    for (int y = 0; y < WATER_H; ++y) for (int x = 0; x < WATER_W; ++x) {
        int d = s_water[y][x] >= WATER_THRESHOLD || outside_case(x, y) ? 255 : 0;
        if (d && x > 0) d = min_i(d, s_tone[y][x - 1] + 3);
        if (d && y > 0) {
            d = min_i(d, s_tone[y - 1][x] + 3);
            if (x > 0) d = min_i(d, s_tone[y - 1][x - 1] + 4);
            if (x < WATER_W - 1) d = min_i(d, s_tone[y - 1][x + 1] + 4);
        }
        s_tone[y][x] = (uint8_t)d;
    }
    for (int y = WATER_H - 1; y >= 0; --y) for (int x = WATER_W - 1; x >= 0; --x) {
        int d = s_tone[y][x];
        if (d && x < WATER_W - 1) d = min_i(d, s_tone[y][x + 1] + 3);
        if (d && y < WATER_H - 1) {
            d = min_i(d, s_tone[y + 1][x] + 3);
            if (x < WATER_W - 1) d = min_i(d, s_tone[y + 1][x + 1] + 4);
            if (x > 0) d = min_i(d, s_tone[y + 1][x - 1] + 4);
        }
        s_tone[y][x] = (uint8_t)d;
    }
    // Estimate flatness from mean particle depth near the front or back wall.
    // Reduce apparent absorption depth as this estimate approaches one.
    float mean_z = 0.0f;
    for (int i = 0; i < n; ++i) mean_z += s_snapshot[i].z;
    mean_z = n ? mean_z / n : BOX_D * 0.5f;
    float flat = (fabsf(mean_z - BOX_D * 0.5f) - BOX_D * 0.15f) / (BOX_D * 0.25f);
    flat = flat < 0.0f ? 0.0f : flat > 1.0f ? 1.0f : flat;
    const int depth_scale = (int)(256.0f * (1.0f - flat)), flat_depth = (int)(WATER_FLAT_DEPTH * flat);
    if (mode == RENDER_LAVA) {
        // Increase lava brightness with depth and with flow above LAVA_FLOW_SPEED.
        for (int y = 0; y < WATER_H; ++y) for (int x = 0; x < WATER_W; ++x) {
            const int depth = s_tone[y][x];
            int tone = s_lava_depth[min_i((depth * depth_scale >> 8) + flat_depth, 255)];
            if (s_water[y][x]) {
                const int speed = s_speed[y][x] * 64 / s_water[y][x];
                if (speed > LAVA_FLOW_SPEED) tone += min_i(speed - LAVA_FLOW_SPEED, LAVA_FLOW_MAX);
            }
            s_tone[y][x] = (uint8_t)min_i(tone, 255);
        }
    } else if (mode == RENDER_UKIYOE) {
        // Palette index is px below the foam line; faster surface water carries a thicker crest.
        for (int y = 0; y < WATER_H; ++y) for (int x = 0; x < WATER_W; ++x) {
            const int depth = s_tone[y][x];
            int foam = UKIYO_FOAM_PX;
            if (depth < WATER_FOAM_DEPTH * 2 && s_water[y][x]) {
                const int speed = s_speed[y][x] * 64 / s_water[y][x];
                if (speed > UKIYO_CREST_SPEED) foam += min_i(speed - UKIYO_CREST_SPEED, UKIYO_CREST_MAX) * UKIYO_CREST_GAIN >> 4;
            }
            // Shift flat-pool tones toward deeper blue while retaining surface stripes.
            const int v = depth * WATER_GRID_STEP / 3 + (int)(UKIYO_FLAT_PX * flat) + UKIYO_FOAM_LINE - foam;
            s_tone[y][x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    } else {
        // Fast water near the surface turns to foam; the bulk only darkens with depth.
        float mean_speed = 0.0f;
        for (int i = 0; i < n; ++i) mean_speed += s_snapshot[i].speed;
        mean_speed = n ? mean_speed / n : 0.0f;
        // Reduce screen-space caustic contrast as mean particle speed increases.
        float calm = 1.0f - (mean_speed - WATER_CAUSTIC_CALM) / WATER_CAUSTIC_CALM;
        calm = calm < 0.0f ? 0.0f : calm > 1.0f ? 1.0f : calm;
        s_caustic_gain += ((int)(calm * 256.0f) - s_caustic_gain) / 8;
        for (int sum = 0; sum < 511; ++sum) s_light[sum] = (uint16_t)(sum * sum * s_caustic_gain >> 16);
        for (int y = 0; y < WATER_H; ++y) for (int x = 0; x < WATER_W; ++x) {
            const int depth = s_tone[y][x];
            int tone = s_depth_tone[min_i((depth * depth_scale >> 8) + flat_depth, 255)];
            if (depth < WATER_FOAM_DEPTH && s_water[y][x]) {
                const int speed = s_speed[y][x] * 64 / s_water[y][x];
                if (speed > WATER_FOAM_SPEED)
                    tone += min_i(speed - WATER_FOAM_SPEED, WATER_FOAM_MAX) * (WATER_FOAM_DEPTH - depth) / WATER_FOAM_DEPTH;
            }
            s_tone[y][x] = (uint8_t)min_i(tone, 255);
        }
    }
    // One separable [1 2 1] pass hides the octagonal chamfer facets.
    for (int y = 0; y < WATER_H; ++y) {
        int prev = s_tone[y][0];
        for (int x = 0; x < WATER_W; ++x) {
            const int cur = s_tone[y][x], next = s_tone[y][x < WATER_W - 1 ? x + 1 : x];
            s_tone[y][x] = (uint8_t)((prev + 2 * cur + next) >> 2);
            prev = cur;
        }
    }
    for (int x = 0; x < WATER_W; ++x) {
        int prev = s_tone[0][x];
        for (int y = 0; y < WATER_H; ++y) {
            const int cur = s_tone[y][x], next = s_tone[y < WATER_H - 1 ? y + 1 : y][x];
            s_tone[y][x] = (uint8_t)((prev + 2 * cur + next) >> 2);
            prev = cur;
        }
    }
}

static inline int clamp_cell(int v) { return v < 0 ? 0 : v >= WATER_W ? WATER_W - 1 : v; }

// Estimate the filled fraction of case depth in [0, 1]. Smoothstep limits density
// above LIQUID_FULL to full thickness without an abrupt shading transition.
static inline float thickness(int x, int y)
{
    const float t = (s_water[clamp_cell(y)][clamp_cell(x)] - WATER_THRESHOLD) * (1.0f / (LIQUID_FULL - WATER_THRESHOLD));
    return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
}

// Compute liquid palette indices from two attenuated light paths: through the
// screen plane and along the view direction. The smoothed gravity input sets
// their relative weights; particle-carried foam increases the resulting tone.
static void shade_liquid(void)
{
    enum { S = WATER_GRID_STEP, N = WATER_W };
    const float ux = s_up[0], uy = s_up[1], uz = s_up[2], lxy2 = ux * ux + uy * uy;
    const float side = lxy2 / (lxy2 + uz * uz), front = 1.0f - side, lxy = sqrtf(lxy2);
    // Sweep the screen-plane light path opposite the projected up direction.
    const float dx = lxy > 1e-3f ? -ux / lxy : 0.0f, dy = lxy > 1e-3f ? -uy / lxy : 1.0f;
    const bool along_x = fabsf(dx) > fabsf(dy);
    const float major = along_x ? dx : dy, slope = (along_x ? dy : dx) / fabsf(major), step = S / fabsf(major);
    static float line[2][N];  // light path crossed so far, per line of the sweep
    for (int m = 0; m < N; ++m) {
        const int a = major > 0 ? m : N - 1 - m;
        float *cur = line[m & 1];
        const float *prev = line[~m & 1];
        for (int k = 0; k < N; ++k) {
            const int x = along_x ? a : k, y = along_x ? k : a;
            const float fill = thickness(x, y);
            float path = 0.0f;
            if (m > 0) {  // Boundary light enters with zero accumulated path length.
                const float src = k - slope;
                const int k0 = (int)floorf(src);
                const float t = src - k0;
                if (k0 >= 0 && k0 + 1 < N) path = prev[k0] + (prev[k0 + 1] - prev[k0]) * t;
            }
            cur[k] = path += fill * step;
            const float lit = side * expf(-path / LIQUID_ABSORB_PX) + front * expf(-2.0f * fill * BOX_D / LIQUID_FRONT_ABSORB_PX);
            float tone = WATER_DEEP_TONE + (WATER_SURFACE_TONE - WATER_DEEP_TONE) * lit;
            if (s_water[y][x]) tone += LIQUID_FOAM_GAIN * (s_speed[y][x] * 64 / s_water[y][x]);
            s_tone[y][x] = (uint8_t)(tone < 255.0f ? tone : 255.0f);
        }
    }
}

// Assign a spray radius only to particles outside the density-defined liquid body.
static void mark_droplets(int n)
{
    enum { S = WATER_GRID_STEP };
    for (int i = 0; i < n; ++i) {
        const int x = s_sx[i] < 0 ? 0 : s_sx[i] >= LCD_H_RES ? LCD_H_RES - 1 : s_sx[i];
        const int y = s_sy[i] < 0 ? 0 : s_sy[i] >= LCD_V_RES ? LCD_V_RES - 1 : s_sy[i];
        const int gx = x / S, gy = y / S, fx = x % S, fy = y % S;
        const int d = (s_water[gy][gx] * (S - fx) + s_water[gy][gx + 1] * fx) * (S - fy)
                    + (s_water[gy + 1][gx] * (S - fx) + s_water[gy + 1][gx + 1] * fx) * fy;
        s_sr[i] = d < WATER_THRESHOLD * S * S ? LIQUID_DROP_R : 0;
        int tone = 225 + (int)(s_snapshot[i].speed / 100.0f);
        s_sc[i] = s_water_lut[0][tone < 255 ? tone : 255];
        s_sh[i] = s_water_lut[0][255];
    }
}

static void draw_liquid(uint16_t *buf, int y0, int left, int width)
{
    enum {
        S = WATER_GRID_STEP,
        EDGE_LO = (WATER_THRESHOLD - WATER_EDGE) * S * S,
        SURFACE = WATER_THRESHOLD * S * S,
        RIM = (WATER_THRESHOLD + WATER_RIM) * S * S,
        TONE_SCALE = 65536 / (S * S) + 1,
    };
    static int cd[WATER_W], ct[WATER_W];
    const int right = left + width;
    const int g0 = left / S, g1 = (right - 1) / S + 1;
    for (int y = y0; y < y0 + BAND_ROWS; ++y) {
        const int gy = y / S, fy = y % S;
        for (int g = g0; g <= g1; ++g) {
            cd[g] = s_water[gy][g] * (S - fy) + s_water[gy + 1][g] * fy;
            ct[g] = (s_tone[gy][g] * (S - fy) + s_tone[gy + 1][g] * fy) * TONE_SCALE;
        }
        uint16_t *row = buf + (y - y0) * width;
        for (int g = g0; g < g1; ++g) {
            const int xa = g * S > left ? g * S : left, xb = g * S + S < right ? g * S + S : right;
            const int d0 = cd[g], d1 = cd[g + 1];
            if (d0 * S < EDGE_LO && d1 * S < EDGE_LO) {
                for (int x = xa; x < xb; ++x) row[x - left] = 0;
                continue;
            }
            const int fx = xa - g * S, dd = d1 - d0, dt = ct[g + 1] - ct[g];
            int d = d0 * (S - fx) + d1 * fx, t = ct[g] * (S - fx) + ct[g + 1] * fx;
            if (d0 * S >= RIM && d1 * S >= RIM) {  // inside the body: the shading is all in the tone
                for (int x = xa; x < xb; ++x, t += dt) row[x - left] = s_poster_lut[t >> 16];
                continue;
            }
            for (int x = xa; x < xb; ++x, d += dd, t += dt) {
                uint16_t color = 0;
                if (d >= SURFACE) {
                    int tone = t >> 16;
                    if (d < RIM) tone += (RIM - d) * WATER_RIM_BOOST / (RIM - SURFACE);
                    color = s_poster_lut[tone < 255 ? tone : 255];
                } else if (d >= EDGE_LO) {
                    color = s_edge_lut[(d - EDGE_LO) * WATER_EDGE_STEPS / (SURFACE - EDGE_LO)];
                }
                row[x - left] = color;
            }
        }
    }
}

// Mottle at grid nodes g0..g1 of pixel row y, scaled so the bilinear cell weights give strength << 16.
static void mottle_row(int *cm, int y, int g0, int g1)
{
    enum { S = WATER_GRID_STEP, M = MOTTLE_PX, K = 65536 * 256 / (M * M * S) };
    const int my = y / M, fy = y % M;
    for (int g = g0; g <= g1; ++g) {
        const int x = g * S, mx = x / M, fx = x % M;
        const int top = s_mottle[my][mx] * (M - fx) + s_mottle[my][mx + 1] * fx;
        const int bot = s_mottle[my + 1][mx] * (M - fx) + s_mottle[my + 1][mx + 1] * fx;
        cm[g] = (top * (M - fy) + bot * fy) * K >> 8;
    }
}

static void draw_water(uint16_t *buf, int y0, int left, int width, int64_t now_us)
{
    enum {
        S = WATER_GRID_STEP,
        EDGE_LO = (WATER_THRESHOLD - WATER_EDGE) * S * S,
        SURFACE = WATER_THRESHOLD * S * S,
        RIM = (WATER_THRESHOLD + WATER_RIM) * S * S,
        TONE_SCALE = 65536 / (S * S) + 1,  // tone >> 16 undoes the bilinear weights
    };
    static int cd[WATER_W], ct[WATER_W], cm[WATER_W];
    static uint8_t col1[LCD_H_RES], col2[LCD_H_RES];
    const int right = left + width;
    const int g0 = left / S, g1 = (right - 1) / S + 1;
    // Compute drifting texture offsets in 64-bit arithmetic before wrapping to 64 texels.
    // Sampling the second layer at 5/8 scale reduces coincident tile repetition.
    const int ax1 = (int)(now_us * 13 / 1000000 & 63), ay1 = (int)(now_us * 7 / 1000000 & 63);
    const int ax2 = (int)(now_us * 9 / 1000000 & 63), ay2 = (int)(now_us * 11 / 1000000 & 63);
    for (int x = left; x < right; ++x) {
        col1[x] = (uint8_t)((x + ax1) & 63);
        col2[x] = (uint8_t)(((x * 5 >> 3) - ax2) & 63);
    }
    for (int y = y0; y < y0 + BAND_ROWS; ++y) {
        const int gy = y / S, fy = y % S;
        for (int g = g0; g <= g1; ++g) {
            cd[g] = s_water[gy][g] * (S - fy) + s_water[gy + 1][g] * fy;
            ct[g] = (s_tone[gy][g] * (S - fy) + s_tone[gy + 1][g] * fy) * TONE_SCALE;
        }
        mottle_row(cm, y, g0, g1);
        uint16_t *row = buf + (y - y0) * width;
        const uint16_t (*lut)[256] = s_water_lut;
        const int row_phase = 5 * y;
        const uint8_t *layer1 = s_caustic[(y + ay1) & 63], *layer2 = s_caustic[((y * 5 >> 3) - ay2) & 63];
        for (int g = g0; g < g1; ++g) {
            const int xa = g * S > left ? g * S : left, xb = g * S + S < right ? g * S + S : right;
            const int d0 = cd[g], d1 = cd[g + 1];
            if (d0 * S < EDGE_LO && d1 * S < EDGE_LO) {  // bilinear density cannot reach the edge
                for (int x = xa; x < xb; ++x) row[x - left] = 0;
                continue;
            }
            // Incremental interpolation from the cell origin keeps cropped and full windows identical.
            const int fx = xa - g * S, dd = d1 - d0, dt = ct[g + 1] - ct[g];
            int d = d0 * (S - fx) + d1 * fx, t = ct[g] * (S - fx) + ct[g + 1] * fx;
            // Deep water below the caustic band needs neither edge, surface line nor light.
            if (d0 * S >= RIM && d1 * S >= RIM && (ct[g] > ct[g + 1] ? ct[g] : ct[g + 1]) * S >> 16 <= WATER_CAUSTIC_TONE) {
                for (int x = xa; x < xb; ++x, t += dt) row[x - left] = lut[dither_phase(x, row_phase)][t >> 16];
                continue;
            }
            const int dm = cm[g + 1] - cm[g];
            int m = cm[g] * (S - fx) + cm[g + 1] * fx;
            for (int x = xa; x < xb; ++x, d += dd, t += dt, m += dm) {
                uint16_t color = 0;
                if (d >= SURFACE) {
                    int tone = t >> 16;
                    if (tone > WATER_CAUSTIC_TONE) {
                        // A mottle value of 128 gives 1x brightness; 255 gives approximately 2x.
                        const int light = s_light[layer1[col1[x]] + layer2[col2[x]]] * (m >> 16) >> 7;
                        tone += light * (tone - WATER_CAUSTIC_TONE) >> (WATER_CAUSTIC_SHIFT - 8);
                    }
                    if (d < RIM) tone += (RIM - d) * WATER_RIM_BOOST / (RIM - SURFACE);
                    color = lut[dither_phase(x, row_phase)][tone < 255 ? tone : 255];
                } else if (d >= EDGE_LO) {
                    color = s_edge_lut[(d - EDGE_LO) * WATER_EDGE_STEPS / (SURFACE - EDGE_LO)];
                }
                row[x - left] = color;
            }
        }
    }
}

static void draw_lava(uint16_t *buf, int y0, int left, int width, int64_t now_us)
{
    enum {
        S = WATER_GRID_STEP,
        EDGE_LO = (WATER_THRESHOLD - WATER_EDGE) * S * S,
        SURFACE = WATER_THRESHOLD * S * S,
        RIM = (WATER_THRESHOLD + WATER_RIM) * S * S,
        TONE_SCALE = 65536 / (S * S) + 1,
    };
    static int cd[WATER_W], ct[WATER_W], cm[WATER_W];
    const int right = left + width;
    const int g0 = left / S, g1 = (right - 1) / S + 1;
    // Crack coordinates remain fixed in screen space while brightness varies periodically.
    const float phase = (float)(now_us % (LAVA_PULSE_MS * 1000LL)) / (LAVA_PULSE_MS * 1000.0f);
    const int glow = (int)(256.0f * (0.75f + 0.25f * sinf(phase * 2.0f * (float)M_PI)));
    for (int y = y0; y < y0 + BAND_ROWS; ++y) {
        const int gy = y / S, fy = y % S;
        for (int g = g0; g <= g1; ++g) {
            cd[g] = s_water[gy][g] * (S - fy) + s_water[gy + 1][g] * fy;
            ct[g] = (s_tone[gy][g] * (S - fy) + s_tone[gy + 1][g] * fy) * TONE_SCALE;
        }
        mottle_row(cm, y, g0, g1);
        uint16_t *row = buf + (y - y0) * width;
        const uint16_t (*lut)[256] = s_lava_lut;
        const int row_phase = 5 * y;
        // Crack layers at 1/2 and 3/8 scale; the second breaks up the 64-texel tile repeat.
        const uint8_t *cracks = s_caustic[(y >> 1) & 63], *cracks2 = s_caustic[((y * 3 >> 3) + 23) & 63];
        for (int g = g0; g < g1; ++g) {
            const int xa = g * S > left ? g * S : left, xb = g * S + S < right ? g * S + S : right;
            const int d0 = cd[g], d1 = cd[g + 1];
            if (d0 * S < EDGE_LO && d1 * S < EDGE_LO) {
                for (int x = xa; x < xb; ++x) row[x - left] = 0;
                continue;
            }
            const int fx = xa - g * S, dd = d1 - d0, dt = ct[g + 1] - ct[g];
            int d = d0 * (S - fx) + d1 * fx, t = ct[g] * (S - fx) + ct[g + 1] * fx;
            // The molten core is past the crust: no cracks, no edge.
            if (d0 * S >= RIM && d1 * S >= RIM && (ct[g] < ct[g + 1] ? ct[g] : ct[g + 1]) * S >> 16 >= LAVA_CRACK_TONE) {
                for (int x = xa; x < xb; ++x, t += dt) row[x - left] = lut[dither_phase(x, row_phase)][t >> 16];
                continue;
            }
            const int dm = cm[g + 1] - cm[g];
            int m = cm[g] * (S - fx) + cm[g + 1] * fx;
            for (int x = xa; x < xb; ++x, d += dd, t += dt, m += dm) {
                uint16_t color = 0;
                if (d >= SURFACE) {
                    int tone = t >> 16;
                    if (tone < LAVA_CRACK_TONE) {
                        // Saturate the primary crack layer to widen seams and add a weaker second layer.
                        // Mottle also offsets texture coordinates to vary seam spacing.
                        const int warp = m >> 19;
                        const int a = min_i(cracks[((x + warp) >> 1) & 63] * 2, 255), b = cracks2[(((x - warp) * 3 >> 3) + 41) & 63] >> 1;
                        const int crack = a > b ? a : b;
                        // Modulate crack contrast by the periodic glow and local mottle strength.
                        tone += crack * (glow * (m >> 16) * 3 >> 9) * (LAVA_CRACK_TONE - tone) >> 16;
                    }
                    if (d < RIM) tone += (RIM - d) * LAVA_RIM_BOOST / (RIM - SURFACE);
                    color = lut[dither_phase(x, row_phase)][tone < 255 ? tone : 255];
                } else if (d >= EDGE_LO) {
                    color = s_lava_edge[(d - EDGE_LO) * WATER_EDGE_STEPS / (SURFACE - EDGE_LO)];
                }
                row[x - left] = color;
            }
        }
    }
}

static void draw_ukiyoe(uint16_t *buf, int y0, int left, int width)
{
    enum {
        S = WATER_GRID_STEP,
        EDGE_LO = (WATER_THRESHOLD - WATER_EDGE) * S * S,
        SURFACE = WATER_THRESHOLD * S * S,
        INK = (WATER_THRESHOLD + UKIYO_INK) * S * S,
        NEAR = UKIYO_FOAM_LINE + UKIYO_DOT_DEPTH,  // Palette cutoff beyond which foam texture is skipped.
        TONE_SCALE = 65536 / (S * S) + 1,
    };
    static int cd[WATER_W], ct[WATER_W];
    const int right = left + width;
    const int g0 = left / S, g1 = (right - 1) / S + 1;
    for (int y = y0; y < y0 + BAND_ROWS; ++y) {
        const int gy = y / S, fy = y % S;
        for (int g = g0; g <= g1; ++g) {
            cd[g] = s_water[gy][g] * (S - fy) + s_water[gy + 1][g] * fy;
            ct[g] = (s_tone[gy][g] * (S - fy) + s_tone[gy + 1][g] * fy) * TONE_SCALE;
        }
        uint16_t *row = buf + (y - y0) * width;
        const uint16_t paper_even = s_paper[0][y], paper_odd = s_paper[1][y];
        const uint8_t *tex = s_foam_tex[y & 63];
        for (int g = g0; g < g1; ++g) {
            const int xa = g * S > left ? g * S : left, xb = g * S + S < right ? g * S + S : right;
            const int d0 = cd[g], d1 = cd[g + 1];
            if (d0 * S < EDGE_LO && d1 * S < EDGE_LO) {
                for (int x = xa; x < xb; ++x) row[x - left] = x & 1 ? paper_odd : paper_even;
                continue;
            }
            const int fx = xa - g * S, dd = d1 - d0, dt = ct[g + 1] - ct[g];
            int d = d0 * (S - fx) + d1 * fx, t = ct[g] * (S - fx) + ct[g + 1] * fx;
            if (d0 * S >= INK && d1 * S >= INK && (ct[g] < ct[g + 1] ? ct[g] : ct[g + 1]) * S >> 16 >= NEAR) {
                for (int x = xa; x < xb; ++x, t += dt) row[x - left] = s_ukiyo_lut[t >> 16];
                continue;
            }
            for (int x = xa; x < xb; ++x, d += dd, t += dt) {
                uint16_t color;
                if (d < EDGE_LO) color = x & 1 ? paper_odd : paper_even;
                else if (d < SURFACE) color = s_ink_soft;
                else if (d < INK) color = s_ukiyo_lut[UKIYO_FOAM_LINE];  // ink outline
                else {
                    int v = t >> 16;
                    if (v < NEAR) v -= tex[x & 63] * s_foam_amp[x] >> 7;  // 128 = texture as drawn
                    color = s_ukiyo_lut[v < 0 ? 0 : v];
                }
                row[x - left] = color;
            }
        }
    }
}

render_stats_t render_frame(bool full_width)
{
    render_stats_t stats = {0};
    const int64_t start = esp_timer_get_time();
    const int n = sim_snapshot(s_snapshot, PARTICLE_MAX);
    const render_mode_t mode = (render_mode_t)atomic_load(&s_mode_requested);
    const bool water = mode != RENDER_PARTICLES;
    project_all(n, water);
    if (water) build_water(n, mode);
    if (mode == RENDER_LIQUID) {
        update_up();
        shade_liquid();
        mark_droplets(n);
    }
    // Paper covers the whole panel, so every band is sent in full.
    if (mode == RENDER_UKIYOE) for (int b = 0; b < BAND_COUNT; ++b) s_extent[b] = (band_extent_t){0, LCD_H_RES};
    stats.prep_us = (uint32_t)(esp_timer_get_time() - start);

    for (int band = 0; band < BAND_COUNT; band++) {
        int left = s_extent[band].left < s_previous[band].left ? s_extent[band].left : s_previous[band].left;
        int right = s_extent[band].right > s_previous[band].right ? s_extent[band].right : s_previous[band].right;
        if (left >= right) continue;
        if (left < 0) left = 0;
        if (right > LCD_H_RES) right = LCD_H_RES;
        if (left >= right) continue;
        // Align the union of current and previous extents, including pixels to erase.
        // A 16-pixel-wide RGB565 window over 30 rows spans whole 64-byte cache lines,
        // allowing direct DMA from the aligned PSRAM frame slot without a bounce buffer.
        left &= ~15;
        right = (right + 15) & ~15;
        if (right > LCD_H_RES) right = LCD_H_RES;
        if (full_width) { left = 0; right = LCD_H_RES; }
        const int width = right - left;
        const int band_y0 = band * BAND_ROWS;
        const int band_y1 = band_y0 + BAND_ROWS;
        uint16_t *buf = s_band;
        if (!water) memset(buf, 0, width * BAND_ROWS * sizeof(*buf));

        if (mode == RENDER_UKIYOE) draw_ukiyoe(buf, band_y0, left, width);
        else if (mode == RENDER_LAVA) draw_lava(buf, band_y0, left, width, start);
        else if (mode == RENDER_LIQUID) {
            draw_liquid(buf, band_y0, left, width);
            for (int i = 0; i < n; ++i) {
                const int r = s_sr[i], cy = s_sy[i];
                if (!r || cy + r < band_y0 || cy - r >= band_y1) continue;
                draw_disc(buf, band_y0, left, width, s_sx[i], cy, r, s_sc[i]);
                draw_disc(buf, band_y0, left, width, s_sx[i] - 1, cy - 1, 1, s_sh[i]);
            }
        }
        else if (water) draw_water(buf, band_y0, left, width, start);
        else for (int i = n - 1; i >= 0; i--) {
            const int r = s_sr[i];
            const int cy = s_sy[i];
            if (cy + r < band_y0 || cy - r >= band_y1) continue;
            draw_disc(buf, band_y0, left, width, s_sx[i], cy, r, s_sc[i]);
#if HIGHLIGHT_ENABLE
            if (r >= 3) {
                draw_disc(buf, band_y0, left, width, s_sx[i] - r / 3, cy - r / 3, r / 2, s_sh[i]);
            }
#endif
        }
        const int64_t stage_start = esp_timer_get_time();
        display_stage_band(band, left, right, buf);
        stats.stage_us += esp_timer_get_time() - stage_start;
        stats.bytes += width * BAND_ROWS * sizeof(uint16_t);
        stats.bands++;
    }
    const int64_t present_start = esp_timer_get_time();
    display_present();
    stats.slot_wait_us = esp_timer_get_time() - present_start;
    memcpy(s_previous, s_extent, sizeof(s_extent));
    stats.cpu_us = (uint32_t)(esp_timer_get_time() - start) - stats.stage_us - stats.slot_wait_us;
    return stats;
}
