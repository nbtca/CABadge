#pragma once
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define FLUIDBOX_RAM EXT_RAM_BSS_ATTR
#else
#define FLUIDBOX_RAM
#endif
#define LCD_H_RES 360
#define LCD_V_RES 360
#define BAND_ROWS 30
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)
#define DISPLAY_BRIGHTNESS_PCT 60  // Converted to register 0x51 units as percent * 255 / 100.
#define BOX_W 360.0f
#define BOX_H 360.0f
#define BOX_D 56.25f
#define PX_PER_METER 9375.0f
#define PX_PER_MM (PX_PER_METER / 1000.0f)
#define BOX_CORNER_MM 4.5f
#define BOX_CORNER_R (BOX_W * 0.5f)  // CABadge's round case uses the existing rounded-box constraint.
#define BOX_BACK_FILLET_MM 2.0f
#define BOX_BACK_FILLET (BOX_BACK_FILLET_MM * PX_PER_MM)
#define BOX_FRONT_FILLET (BOX_BACK_FILLET * 0.25f)
#define TIME_SCALE 0.140f
// Limit pressure-driven agitation in dense side-wall and corner accumulations.
#define SIM_DT_MAX 0.0010f
#define GRAVITY_MPS2 9.81f
#define GRAVITY_GAIN 2.2f
#define PARTICLE_MAX 1200
#define PARTICLE_COUNT 1200
#define REST_SPACING 12.75f
#define SMOOTH_RADIUS 21.0f
#define SUBSTEPS 1
#define K_PRESSURE 300000.0f
#define K_NEAR_PRESSURE 600000.0f
#define MAX_DISPLACEMENT 3.0f
#define WALL_JITTER 0.2625f
#define VISC_SIGMA 45.0f
#define VISC_BETA 0.03f
#define WALL_RESTITUTION 0.25f
// Foam begins above FOAM_HIT_SPEED in the screen-plane wall-normal velocity (px/s).
// It reaches full strength at FOAM_HIT_SPEED + FOAM_HIT_RANGE.
#define FOAM_HIT_SPEED 900.0f
#define FOAM_HIT_RANGE 2000.0f
#define FOAM_FADE 252             // Foam is multiplied by this value / 256 each substep, with integer truncation.
#define WALL_FRICTION 0.96f
#define GRAVITY_LP_HZ 2.0f
#define SHAKE_GAIN 1.6f
#define ROTATION_GAIN 1.0f
// Map BMI270 acceleration and angular velocity to simulation axes:
// x points right, y points down the screen, and z points into the case.
#define IMU_MAP_X(ax, ay, az) (ax)
#define IMU_MAP_Y(ax, ay, az) (-(ay))
#define IMU_MAP_Z(ax, ay, az) (-(az))
#define PROJ_FOCAL 165.0f
#define PARTICLE_RADIUS_PX 4.875f
#define DISC_MAX_R 10
#define HIGHLIGHT_ENABLE 1
#define HIGHLIGHT_LIFT 0.55f  // Blend toward white before depth dimming: 0 = unchanged, 1 = white.
#define SPEED_LEVELS 64
#define DEPTH_LEVELS 16
#define SPEED_COLOR_MAX 5000.0f
#define SPEED_COLOR_GAMMA 0.55f
#define DEPTH_DIM_MIN 0.60f

// Water surface sampling and projected particle influence, in screen pixels.
#define WATER_GRID_STEP 6
#define WATER_RADIUS_PX 30
#define WATER_THRESHOLD 80
// Density units below/above the threshold for the antialiased edge and the bright surface line.
#define WATER_EDGE 8
#define WATER_EDGE_STEPS 8
#define WATER_RIM 30
#define WATER_RIM_BOOST 40
// Palette indices 0..255 run navy -> aqua -> white; the bulk fades from surface to deep tone.
#define WATER_SURFACE_TONE 212
#define WATER_DEEP_TONE 24
#define WATER_RIM_TONE 245
#define WATER_ABSORB_PX 60.0f
// Foam starts at speed / 32 above this value, within this chamfer depth (3 units per grid cell).
#define WATER_FOAM_SPEED 100
#define WATER_FOAM_DEPTH 12
#define WATER_FOAM_MAX 80
// Chamfer depth used when the box lies flat and light crosses only the thin layer.
#define WATER_FLAT_DEPTH 8
// Caustics (two drifting layers, summed and squared) brighten only water above this palette index.
#define WATER_CAUSTIC_TONE 110
#define WATER_CAUSTIC_SHIFT 18
#define WATER_CAUSTIC_CELLS 5
#define WATER_CAUSTIC_CALM 900.0f  // Mean speed (px/s) where the caustic target starts fading; zero at twice this value.
// Raise density using a wider blur * GAIN / 4 - BIAS to reduce interior gaps.
// Stronger filling also expands the rendered surface.
#define WATER_FILL_GAIN 6
#define WATER_FILL_BIAS 20
// Random light strength sampled every MOTTLE_PX breaks up the 64 px caustic and crack tiles.
#define MOTTLE_PX 40
#define MOTTLE_MIN 70           // weakest patch, of 255

// Ukiyo-e mode: palette index = px below the foam line + UKIYO_FOAM_LINE.
#define UKIYO_FOAM_LINE 64
#define UKIYO_INK 25          // density units of ink outline inside the water edge
#define UKIYO_FOAM_PX 5       // calm foam lip
#define UKIYO_CREST_SPEED 45  // speed / 32 where crests start thickening
#define UKIYO_CREST_MAX 60
#define UKIYO_CREST_GAIN 5    // px of extra foam per 16 speed units
#define UKIYO_LOBES 9
#define UKIYO_LOBE_R 13.0f
#define UKIYO_LOBE_PX 14.0f
#define UKIYO_DOTS 7
#define UKIYO_DOT_DEPTH 34
#define UKIYO_FLAT_PX 70
#define UKIYO_FOAM_VARY_PX 56   // spacing of random foam-size samples along the crest
#define UKIYO_FOAM_MIN 70       // Minimum foam scale; 128 is 1x and 255 is approximately 2x.

// Lava mode: palette index 0..255 runs crust black -> red -> orange -> yellow-white.
#define LAVA_CRUST_TONE 22      // cooled surface
#define LAVA_CORE_TONE 175      // molten interior
#define LAVA_CRUST_PX 70.0f     // depth over which the crust gives way to the core
#define LAVA_CRACK_TONE 235     // cracks in the crust glow toward this
#define LAVA_RIM_TONE 235
#define LAVA_RIM_BOOST 70
#define LAVA_FLOW_SPEED 60      // speed / 32 where flow starts heating the lava
#define LAVA_FLOW_MAX 35
#define LAVA_PULSE_MS 3000      // Crack brightness modulation period, ms.
#define LAVA_TIME_SCALE 0.5f    // Additional time scale for slower apparent flow; does not change viscosity coefficients.

// Liquid mode uses quantized lighting and particle-carried foam. Estimated fill
// increases smoothly from 0 at WATER_THRESHOLD to 1 at LIQUID_FULL; higher density
// is treated as compression rather than additional thickness.
#define LIQUID_FULL 700
#define LIQUID_ABSORB_PX 70.0f    // light path through liquid that dims it to 1/e, sideways
#define LIQUID_FRONT_ABSORB_PX 160.0f  // 1/e attenuation length for the view-direction light path, px.
#define LIQUID_DROP_R 2           // spray droplets outside the body, px
#define LIQUID_FOAM_GAIN 2.2f     // palette steps toward white per unit of carried foam (0..255)

#define DITHER_PHASES 8  // power of two
// The 60 MHz setting reduces the intermittent band corruption observed at 80 MHz.
// At 60 MHz, RGB565 payload transfer takes about 0.96 ms per full-width 30-row band,
// or 15.36 ms per frame, before command overhead. display.c schedules bands behind
// the panel scan to account for transfer time.
#define LCD_PIXEL_CLOCK_HZ 40000000
// QSPI drive strength, GPIO_DRIVE_CAP_0..3; selected for the configured 60 MHz bus.
#define LCD_QSPI_DRIVE_CAP GPIO_DRIVE_CAP_2
_Static_assert(LCD_V_RES % BAND_ROWS == 0 && BAND_ROWS % 2 == 0, "CO5300 bands require complete even row pairs");
_Static_assert(PARTICLE_COUNT <= PARTICLE_MAX, "particle capacity exceeded");
