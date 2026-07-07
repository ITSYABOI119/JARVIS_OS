/* Phase 6 K/M2c — shared PB-contact miss-counter (hang/wedge detector). See km2b_miss.h.
 *
 * Deliberately trivial: the whole point is that the counter moves ONLY here, on explicit
 * timeout/ack calls — never on an implicit poll of a maybe-stale flag (the STEP-3
 * phantom class). Host-tested by test_km2b_miss.c. */

#include "km2b_miss.h"

void km2b_miss_on_pb_timeout(km2b_miss_t *m, uint16_t lane)
{
    if (!m) return;
    m->count++;
    m->last_lane = lane;
}

void km2b_miss_on_pb_ack(km2b_miss_t *m)
{
    if (!m) return;
    m->count = 0;
}

int km2b_miss_tripped(const km2b_miss_t *m, uint32_t threshold)
{
    if (!m || threshold == 0) return 0;   /* threshold 0 would trip at count 0 — refuse */
    return m->count >= threshold;
}

uint32_t km2b_miss_count(const km2b_miss_t *m) { return m ? m->count : 0; }
uint16_t km2b_miss_last_lane(const km2b_miss_t *m) { return m ? m->last_lane : 0; }
