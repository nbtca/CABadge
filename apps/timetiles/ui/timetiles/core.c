#include "core.h"
#include <limits.h>
#include <string.h>

/* Portable reconstruction of recovered v4 core 0x5001af18/b23c/c6d8;
 * original hardware/rendering ABI and unknown state bytes are not retained. */
#define MINUTE_MS INT64_C(60000)
#define DAY_MS INT64_C(86400000)
#define VALID_WALL_MS INT64_C(1704067200000)

static bool wall_valid(int64_t ms)
{
    return ms >= VALID_WALL_MS && ms <= INT64_C(253402300799999);
}

static uint32_t date_key(int64_t local_day)
{
    int64_t z = local_day + 719468, era = z / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    int month = (int)mp + (mp < 10 ? 3 : -9);
    unsigned year = yoe + (unsigned)era * 400 + (month <= 2);
    return year * 10000 + (unsigned)month * 100 + day;
}

static int64_t next_alarm(const tt_core_t *c, const tt_alarm_t *a)
{
    if (!c->wall_valid || !a->present || !a->enabled || a->hour > 23 ||
        a->minute > 59 || a->weekday_mask > 127) return 0;
    int64_t offset = (int64_t)c->timezone_minutes * MINUTE_MS;
    int64_t local_day = (c->wall_ms + offset) / DAY_MS;
    for (int i = 0; i < 8; ++i) {
        int64_t day = local_day + i;
        int64_t candidate = day * DAY_MS - offset +
                            (a->hour * 60 + a->minute) * MINUTE_MS;
        unsigned weekday = (unsigned)((day + 4) % 7);
        if (candidate > c->wall_ms && candidate > a->last_fired_ms &&
            (!a->weekday_mask || (a->weekday_mask & (1u << weekday))))
            return candidate;
    }
    return 0;
}

static void alert(tt_core_t *c, uint8_t type, uint8_t index, bool late)
{
    if (c->alert_count == 10) { ++c->alert_dropped; return; }
    c->alerts[c->alert_count++] = (tt_alert_t){type, index, late};
}

static void timer_duration(struct recovered_timer_block *t, uint32_t ms)
{
    t->status_raw = 0;
    t->duration_ms = t->remaining_ms = ms;
    t->deadline_low = t->deadline_high = 0;
}

static uint32_t focus_duration(const tt_core_t *c)
{
    if (!c->focus_rest) return c->focus_minutes * 60000u;
    return c->cycles && !(c->cycles & 3) ? 900000u : 300000u;
}

uint32_t tt_remaining(const tt_core_t *c, bool focus)
{
    return recovered_timer_remaining_ms(focus ? &c->focus : &c->countdown,
                                        c->monotonic_ms);
}

void tt_init(tt_core_t *c)
{
    memset(c, 0, sizeof(*c));
    c->focus_minutes = 25;
    c->timezone_minutes = 480;
    c->grid = c->glow = true;
    c->revision = 1;
    c->orientation.candidate = c->orientation.accepted = -1;
    timer_duration(&c->focus, 1500000);
    timer_duration(&c->countdown, 600000);
}

bool tt_select(tt_core_t *c, unsigned page, bool auto_start)
{
    if (page > TT_ALARMS) return false;
    bool changed = c->page != page;
    c->page = (uint8_t)page;
    if (auto_start && (page == TT_FOCUS || page == TT_COUNTDOWN)) {
        struct recovered_timer_block *t = page == TT_FOCUS ? &c->focus : &c->countdown;
        changed |= t->status_raw != 1;
        recovered_timer_prepare_start(t, c->monotonic_ms, &c->revision);
    }
    return changed;
}

void tt_toggle(tt_core_t *c, bool focus)
{
    struct recovered_timer_block *t = focus ? &c->focus : &c->countdown;
    if (t->status_raw == 1) {
        t->remaining_ms = tt_remaining(c, focus);
        t->status_raw = 2;
        t->deadline_low = t->deadline_high = 0;
        ++c->revision;
    } else {
        if (focus && t->status_raw == 3) {
            c->focus_rest = !c->focus_rest;
            timer_duration(t, focus_duration(c));
        }
        recovered_timer_prepare_start(t, c->monotonic_ms, &c->revision);
    }
}

void tt_reset(tt_core_t *c, bool focus)
{
    struct recovered_timer_block *t = focus ? &c->focus : &c->countdown;
    if (t->status_raw || t->remaining_ms != t->duration_ms || t->deadline_low || t->deadline_high) {
        timer_duration(t, t->duration_ms);
        ++c->revision;
    }
}

bool tt_focus_duration(tt_core_t *c, unsigned minutes)
{
    if (minutes != 15 && minutes != 25 && minutes != 45 && minutes != 60) return false;
    c->focus_minutes = (uint8_t)minutes;
    c->focus_rest = false;
    timer_duration(&c->focus, minutes * 60000u);
    ++c->revision;
    return true;
}

bool tt_countdown_duration(tt_core_t *c, unsigned seconds)
{
    if (!seconds || seconds > 86399) return false;
    timer_duration(&c->countdown, seconds * 1000u);
    ++c->revision;
    return true;
}

bool tt_alarm_set(tt_core_t *c, unsigned index, unsigned hour, unsigned minute,
                  unsigned mask, bool enabled)
{
    if (index >= 6 || hour > 23 || minute > 59 || mask > 127) return false;
    tt_alarm_t *a = &c->alarms[index];
    bool same_time = a->present && a->hour == hour && a->minute == minute && a->weekday_mask == mask;
    int64_t last = same_time ? a->last_fired_ms : 0;
    *a = (tt_alarm_t){.present = true, .enabled = enabled, .hour = (uint8_t)hour,
                      .minute = (uint8_t)minute, .weekday_mask = (uint8_t)mask,
                      .last_fired_ms = last};
    a->wall_deadline_ms = next_alarm(c, a);
    ++c->revision;
    return true;
}

void tt_alarm_delete(tt_core_t *c, unsigned index)
{
    if (index >= 6 || !c->alarms[index].present) return;
    memset(&c->alarms[index], 0, sizeof(c->alarms[index]));
    ++c->revision;
}

void tt_tick(tt_core_t *c, uint64_t mono_ms, int64_t wall_ms)
{
    if (mono_ms > c->monotonic_ms) c->monotonic_ms = mono_ms;
    c->wall_ms = wall_ms;
    c->wall_valid = wall_valid(wall_ms);
    struct recovered_timer_block *timers[] = {&c->focus, &c->countdown};
    for (int i = 0; i < 2; ++i) {
        struct recovered_timer_block *t = timers[i];
        if (t->status_raw == 1 && !recovered_timer_remaining_ms(t, c->monotonic_ms)) {
            t->status_raw = 3;
            t->remaining_ms = t->deadline_low = t->deadline_high = 0;
            if (!i && !c->focus_rest) { ++c->cycles; ++c->today; }
            alert(c, i ? 2 : c->focus_rest, 0, false);
            ++c->revision;
        }
    }
    if (c->wall_valid) {
        uint32_t date = date_key((wall_ms + (int64_t)c->timezone_minutes * MINUTE_MS) / DAY_MS);
        if (date != c->date_key) { c->date_key = date; c->today = 0; ++c->revision; }
    }
    for (uint8_t i = 0; i < 6; ++i) {
        tt_alarm_t *a = &c->alarms[i];
        if (!a->present) continue;
        if (a->snooze_deadline_ms && a->snooze_deadline_ms <= c->monotonic_ms) {
            a->snooze_deadline_ms = 0;
            alert(c, 3, i, false);
            ++c->revision;
        } else if (c->wall_valid && a->enabled) {
            if (!a->wall_deadline_ms) a->wall_deadline_ms = next_alarm(c, a);
            if (a->wall_deadline_ms && a->wall_deadline_ms <= wall_ms) {
                int64_t scheduled = a->wall_deadline_ms;
                a->last_fired_ms = scheduled;
                if (!a->weekday_mask) a->enabled = false;
                a->wall_deadline_ms = next_alarm(c, a);
                alert(c, 3, i, wall_ms - scheduled > 90000);
                ++c->revision;
            }
        }
    }
}

void tt_ack(tt_core_t *c, bool advance)
{
    if (!c->alert_count) return;
    tt_alert_t a = c->alerts[0];
    --c->alert_count;
    memmove(c->alerts, c->alerts + 1, c->alert_count * sizeof(*c->alerts));
    ++c->revision;
    if (!advance) return;
    if (a.type == 3) {
        if (a.alarm_index < 6 && c->alarms[a.alarm_index].present)
            c->alarms[a.alarm_index].snooze_deadline_ms = c->monotonic_ms + 300000;
    } else if (a.type == 2) {
        recovered_timer_prepare_start(&c->countdown, c->monotonic_ms, &c->revision);
        c->page = TT_COUNTDOWN;
    } else {
        c->focus_rest = a.type == 0;
        timer_duration(&c->focus, focus_duration(c));
        recovered_timer_prepare_start(&c->focus, c->monotonic_ms, &c->revision);
        c->page = TT_FOCUS;
    }
}

bool tt_motion(tt_core_t *c, const int16_t xyz[3], uint64_t mono_ms)
{
    int x = xyz[0], y = xyz[1], z = xyz[2];
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
    int candidate = -1;
    if (az < 761) {
        if (ay > 649 && ay * 4 >= ax * 5) candidate = y < 0 ? 180 : 0;
        else if (ax > 649 && ax * 4 >= ay * 5) candidate = x < 0 ? 270 : 90;
    }
    if (candidate < 0 || candidate == c->orientation.accepted) {
        c->orientation.candidate = -1;
        return false;
    }
    if (candidate != c->orientation.candidate || mono_ms < c->orientation.since) {
        c->orientation.candidate = candidate;
        c->orientation.since = mono_ms;
        return false;
    }
    if (mono_ms - c->orientation.since < 320) return false;
    c->orientation.accepted = candidate;
    c->orientation.candidate = -1;
    if (mono_ms > c->monotonic_ms) c->monotonic_ms = mono_ms;
    tt_select(c, candidate == 90 ? TT_FOCUS : candidate == 180 ? TT_COUNTDOWN :
                 candidate == 270 ? TT_ALARMS : TT_CLOCK, true);
    return true;
}
