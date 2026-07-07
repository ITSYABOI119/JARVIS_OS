/*
 * JARVIS AI-OS — Phase 6 K/M2c: the shared PB-contact miss-counter (hang/wedge detector)
 *
 * STEP-3 closed the CRASH lane (PB faults -> fault-EP NBRecv -> restart). K/M2c closes
 * the OTHER failure mode: PB ALIVE but WEDGED (no fault fires, the fault EP stays
 * silent). Detection = a SHARED consecutive-PB-contact-miss counter: every PB-contact
 * lane (inference / heartbeat / shield) that times out with NO typed response advances
 * it; ANY genuine typed PB dequeue resets it to 0. At threshold, PA funnels the SAME
 * pa_restart_pb("hang", ...) restart the crash lane uses.
 *
 * CORRECTNESS CRUX (the STEP-3 phantom bug was "absence looks like an event"): the
 * counter moves ONLY on EXPLICIT km2b_miss_on_pb_timeout / km2b_miss_on_pb_ack calls —
 * nothing implicit, no polling of a maybe-stale flag. Cache iterations (which never
 * contact PB) call NEITHER: neutrality is caller discipline, enforced by this contract.
 * The reset MUST key off a GENUINE typed dequeue (MSG_RESPONSE / HEARTBEAT_ACK /
 * SHIELD_RESULT), never a bare seL4_Wait (the ~7% stale-signal Heisenbug would mask a
 * wedge). Sound because PB-main serves the ring single-threaded: a wedge kills every
 * lane at once, so any ACK proves PB is alive and serving -> reset to 0.
 *
 * Host-pure: no seL4/device dependency. Design: phase6/docs/PHASE_6_GOAL_K_SYSTEM_DESIGN.md §5-C.
 */

#ifndef KM2B_MISS_H
#define KM2B_MISS_H

#include <stdint.h>

/* PB-contact lane ids for the hang trigger's audit snapshot. 0 is reserved by the crash
 * path (pa_fault_check passes lane=0 for a fault). */
enum {
    KM2B_LANE_INFERENCE = 1,
    KM2B_LANE_HEARTBEAT = 2,
    KM2B_LANE_SHIELD    = 3,
};

typedef struct {
    uint32_t count;      /* consecutive PB-contact timeouts since the last genuine ACK */
    uint16_t last_lane;  /* lane of the most recent timeout (carried into the JACT trigger) */
} km2b_miss_t;

/* A CONFIRMED no-typed-response on a PB-contact lane: advance the consecutive counter and
 * record the lane. Only the inference/heartbeat/shield timeout sites call this. */
void km2b_miss_on_pb_timeout(km2b_miss_t *m, uint16_t lane);

/* A GENUINE typed PB dequeue: PB is alive and serving -> reset the counter to 0. */
void km2b_miss_on_pb_ack(km2b_miss_t *m);

/* 1 iff the wedge is confirmed (count >= threshold). Never trips at count 0 (threshold >= 1). */
int km2b_miss_tripped(const km2b_miss_t *m, uint32_t threshold);

uint32_t km2b_miss_count(const km2b_miss_t *m);
uint16_t km2b_miss_last_lane(const km2b_miss_t *m);

#endif /* KM2B_MISS_H */
