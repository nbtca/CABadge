#include "readable_timer_helpers.h"

/* Native0x5001abf4; complete v3 C and assembly have been inspected.
 * Native reads now at core+0x158/+0x15c, timer fields at+0/+8/+0x10/+0x14.
 * This standalone API expresses the same integer math; names are assigned.
 */
uint32_t recovered_timer_remaining_ms(const struct recovered_timer_block *timer,
                                     uint64_t monotonic_now_ms)
{
    if (timer->status_raw != 1) return timer->remaining_ms;
    uint64_t deadline = ((uint64_t)timer->deadline_high << 32) | timer->deadline_low;
    if (deadline <= monotonic_now_ms) return 0;
    uint64_t delta = deadline - monotonic_now_ms;
    return delta >= UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
}

/* Native0x5001b188, before its repaint tail-call. Native revision is+0x188.
 * Unsigned overflow deliberately preserves RV32 carry/wrap semantics.
 */
void recovered_timer_prepare_start(struct recovered_timer_block *timer,
                                   uint64_t monotonic_now_ms,
                                   uint32_t *state_revision)
{
    if (timer->status_raw == 1) return;
    if (timer->status_raw == 3 || timer->remaining_ms == 0)
        timer->remaining_ms = timer->duration_ms;
    uint64_t deadline = monotonic_now_ms + timer->remaining_ms;
    timer->deadline_low = (uint32_t)deadline;
    timer->deadline_high = (uint32_t)(deadline >> 32);
    timer->status_raw = 1;
    ++*state_revision;
}
