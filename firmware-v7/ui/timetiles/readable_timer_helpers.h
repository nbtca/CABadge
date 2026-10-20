#ifndef TIMETILES_READABLE_TIMER_HELPERS_H
#define TIMETILES_READABLE_TIMER_HELPERS_H
#include <stdint.h>
#include <stddef.h>

/* Analyst-assigned names; exact recovered native block layout (24 bytes).
 * Focus block: business core+0x08. Countdown block: business core+0x20.
 * The business core starts at GSP wrapper+0x08. No original C declaration
 * was available. Status values observed:0 idle,1 running,2 paused,3 finished.
 */
struct recovered_timer_block {
    uint32_t status_raw;
    uint32_t duration_ms;
    uint32_t remaining_ms;
    uint32_t unknown_0c;
    uint32_t deadline_low;
    uint32_t deadline_high;
};
_Static_assert(sizeof(struct recovered_timer_block) == 24, "native timer stride");
_Static_assert(offsetof(struct recovered_timer_block, deadline_low) == 16, "native deadline offset");

uint32_t recovered_timer_remaining_ms(const struct recovered_timer_block *timer,
                                     uint64_t monotonic_now_ms);
/* Verified state arithmetic from0x5001b188. The original then tail-calls
 * repaint invalidation0x5001d8ce; this portable extraction does not draw.
 */
void recovered_timer_prepare_start(struct recovered_timer_block *timer,
                                   uint64_t monotonic_now_ms,
                                   uint32_t *state_revision);
#endif
