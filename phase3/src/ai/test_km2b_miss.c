/*
 * Phase 6 K/M2c — host test for the shared PB-contact miss-counter (§5-C).
 *
 * Pins the state-machine contract pa_restart_pb's hang trigger depends on. The counter
 * must move ONLY on explicit timeout/ack calls (no "absence looks like an event" — the
 * STEP-3 phantom class), reset on any genuine ACK, and trip at >= threshold (never ==).
 *
 * Build: gcc -O2 -I. test_km2b_miss.c km2b_miss.c -o test_km2b_miss && ./test_km2b_miss
 */

#include "km2b_miss.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

int main(void)
{
    printf("=== test_km2b_miss (Phase 6 K/M2c §5-C) ===\n");

    /* T1: fresh counter is 0 and never tripped. */
    km2b_miss_t m = {0};
    CHECK(km2b_miss_count(&m) == 0, "T1a fresh count == 0");
    CHECK(!km2b_miss_tripped(&m, 3), "T1b fresh not tripped at threshold 3");

    /* T2: a single timeout advances the count by exactly 1 and records the lane. */
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_INFERENCE);
    CHECK(km2b_miss_count(&m) == 1, "T2a one timeout -> count 1");
    CHECK(km2b_miss_last_lane(&m) == KM2B_LANE_INFERENCE, "T2b lane recorded (inference)");
    CHECK(!km2b_miss_tripped(&m, 3), "T2c count 1 not tripped at 3");

    /* T3: timeout x2 then ACK resets to 0 (the healthy-recovery case). */
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_HEARTBEAT);
    CHECK(km2b_miss_count(&m) == 2, "T3a two timeouts -> count 2");
    km2b_miss_on_pb_ack(&m);
    CHECK(km2b_miss_count(&m) == 0, "T3b ACK resets to 0");
    CHECK(!km2b_miss_tripped(&m, 3), "T3c after reset not tripped");

    /* T4: three CONSECUTIVE timeouts trip at threshold 3 (the wedge). */
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_INFERENCE);
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_HEARTBEAT);
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_SHIELD);
    CHECK(km2b_miss_count(&m) == 3, "T4a three timeouts -> count 3");
    CHECK(km2b_miss_tripped(&m, 3), "T4b tripped at threshold 3");
    CHECK(km2b_miss_last_lane(&m) == KM2B_LANE_SHIELD, "T4c last lane == shield");

    /* T5: tripped is >= threshold, not ==. A 4th timeout stays tripped. */
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_INFERENCE);
    CHECK(km2b_miss_count(&m) == 4 && km2b_miss_tripped(&m, 3),
          "T5 count 4 still tripped (>=, not ==)");

    /* T6: an ACK mid-climb resets even from a tripped state (recovery clears the window). */
    km2b_miss_on_pb_ack(&m);
    CHECK(km2b_miss_count(&m) == 0 && !km2b_miss_tripped(&m, 3),
          "T6 ACK from tripped -> 0, not tripped");

    /* T7: NEUTRAL — neither call means the count does not move (cache-lane discipline). */
    km2b_miss_on_pb_timeout(&m, KM2B_LANE_INFERENCE);   /* count 1 */
    uint32_t before = km2b_miss_count(&m);
    /* (no call here — a cache iteration) */
    CHECK(km2b_miss_count(&m) == before, "T7 no call -> count unchanged (neutral)");

    /* T8: the interrupted-then-recovered wedge — 2 timeouts, ACK, 3 timeouts trips only on
     * the SECOND run (consecutiveness is real, not cumulative). */
    km2b_miss_t w = {0};
    km2b_miss_on_pb_timeout(&w, KM2B_LANE_HEARTBEAT);
    km2b_miss_on_pb_timeout(&w, KM2B_LANE_HEARTBEAT);   /* count 2, NOT tripped */
    CHECK(!km2b_miss_tripped(&w, 3), "T8a 2 consecutive not tripped");
    km2b_miss_on_pb_ack(&w);                             /* PB blinked -> reset */
    km2b_miss_on_pb_timeout(&w, KM2B_LANE_SHIELD);
    km2b_miss_on_pb_timeout(&w, KM2B_LANE_SHIELD);
    CHECK(!km2b_miss_tripped(&w, 3), "T8b 2 post-reset not tripped (not cumulative)");
    km2b_miss_on_pb_timeout(&w, KM2B_LANE_SHIELD);
    CHECK(km2b_miss_tripped(&w, 3), "T8c 3rd consecutive post-reset trips");

    /* T9: threshold 1 trips on the first timeout; threshold guards count 0. */
    km2b_miss_t o = {0};
    CHECK(!km2b_miss_tripped(&o, 1), "T9a threshold 1 not tripped at count 0");
    km2b_miss_on_pb_timeout(&o, KM2B_LANE_INFERENCE);
    CHECK(km2b_miss_tripped(&o, 1), "T9b threshold 1 trips at count 1");

    /* T10: independent instances don't share state. */
    km2b_miss_t a = {0}, b = {0};
    km2b_miss_on_pb_timeout(&a, KM2B_LANE_INFERENCE);
    km2b_miss_on_pb_timeout(&a, KM2B_LANE_INFERENCE);
    CHECK(km2b_miss_count(&a) == 2 && km2b_miss_count(&b) == 0,
          "T10 independent instances isolated");

    printf("=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
