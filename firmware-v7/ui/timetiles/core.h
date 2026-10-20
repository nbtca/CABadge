#ifndef TIMETILES_CORE_H
#define TIMETILES_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include "readable_timer_helpers.h"

enum { TT_CLOCK, TT_FOCUS, TT_COUNTDOWN, TT_ALARMS };
typedef struct {
    bool present, enabled;
    uint8_t hour, minute, weekday_mask;
    int64_t wall_deadline_ms, last_fired_ms;
    uint64_t snooze_deadline_ms;
} tt_alarm_t;
typedef struct { uint8_t type, alarm_index; bool late; } tt_alert_t;
typedef struct {
    uint8_t page, focus_minutes, theme;
    bool focus_rest, grid, glow, wall_valid;
    uint32_t cycles, today, date_key, revision;
    int16_t timezone_minutes;
    uint64_t monotonic_ms;
    int64_t wall_ms;
    struct recovered_timer_block focus, countdown;
    tt_alarm_t alarms[6];
    tt_alert_t alerts[10];
    uint8_t alert_count;
    uint32_t alert_dropped;
    struct { int candidate, accepted; uint64_t since; } orientation;
} tt_core_t;

void tt_init(tt_core_t *c);
void tt_tick(tt_core_t *c, uint64_t mono_ms, int64_t wall_ms);
bool tt_select(tt_core_t *c, unsigned page, bool orientation_auto_start);
void tt_toggle(tt_core_t *c, bool focus);
void tt_reset(tt_core_t *c, bool focus);
bool tt_focus_duration(tt_core_t *c, unsigned minutes);
bool tt_countdown_duration(tt_core_t *c, unsigned seconds);
bool tt_alarm_set(tt_core_t *c, unsigned index, unsigned hour, unsigned minute,
                  unsigned mask, bool enabled);
void tt_alarm_delete(tt_core_t *c, unsigned index);
void tt_ack(tt_core_t *c, bool snooze_or_continue);
bool tt_motion(tt_core_t *c, const int16_t xyz[3], uint64_t mono_ms);
uint32_t tt_remaining(const tt_core_t *c, bool focus);
#endif
