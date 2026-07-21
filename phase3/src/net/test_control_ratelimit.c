/* test_control_ratelimit.c - unit tests for the control-IN token bucket. */
#include <stdio.h>
#include "control_ratelimit.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

/* T1: steady 1 request/sec -> every one allowed (refill == consumption). */
static void t_steady(void)
{
    control_ratelimit_t rl;
    uint32_t t;
    int all_ok = 1;
    control_ratelimit_init(&rl, 1000u);
    /* Start full; consume then step by 1000 ms each time. */
    for (t = 1000u; t <= 1000u + 30u * 1000u; t += 1000u) {
        if (!control_ratelimit_allow(&rl, t)) {
            all_ok = 0;
        }
    }
    CHECK(all_ok, "steady 1/sec all allowed");
}

/* T2: burst at a single now_ms -> exactly CAP allowed, then drops. */
static void t_burst(void)
{
    control_ratelimit_t rl;
    unsigned i;
    int allowed = 0;
    int dropped_after = 1;
    control_ratelimit_init(&rl, 5000u);
    for (i = 0; i < CONTROL_RL_CAPACITY; i++) {
        if (control_ratelimit_allow(&rl, 5000u)) {
            allowed++;
        }
    }
    /* Next few at the same instant must all drop. */
    for (i = 0; i < 4; i++) {
        if (control_ratelimit_allow(&rl, 5000u)) {
            dropped_after = 0;
        }
    }
    CHECK(allowed == (int)CONTROL_RL_CAPACITY, "burst allows exactly CAP");
    CHECK(dropped_after, "burst drops after CAP at same instant");
}

/* T3: drain, then +8000 ms -> refills up to CAP only (clamped, not CAP+8). */
static void t_drain_refill_clamp(void)
{
    control_ratelimit_t rl;
    unsigned i;
    int allowed_after = 0;
    int dropped_next = 1;
    control_ratelimit_init(&rl, 0u);
    /* Drain the full bucket at t=0. */
    for (i = 0; i < CONTROL_RL_CAPACITY; i++) {
        (void)control_ratelimit_allow(&rl, 0u);
    }
    /* Jump 8000 ms: refills 8 tokens, but capacity is CAP (8) -> at most CAP. */
    for (i = 0; i < CONTROL_RL_CAPACITY + 8u; i++) {
        if (control_ratelimit_allow(&rl, 8000u)) {
            allowed_after++;
        }
    }
    /* Should have allowed exactly CAP (not CAP+8) after the big jump. */
    CHECK(allowed_after == (int)CONTROL_RL_CAPACITY,
          "8s jump refills to CAP only (clamped, not CAP+8)");
    /* And the immediately-next one at the same instant drops. */
    if (control_ratelimit_allow(&rl, 8000u)) {
        dropped_next = 0;
    }
    CHECK(dropped_next, "post-clamp bucket empty drops next");
}

/* T4: uint32 wrap of the millisecond clock refills correctly. */
static void t_wrap(void)
{
    control_ratelimit_t rl;
    unsigned i;
    /* Init full then drain at last_ms = 0xFFFFFF00. */
    control_ratelimit_init(&rl, 0xFFFFFF00u);
    for (i = 0; i < CONTROL_RL_CAPACITY; i++) {
        (void)control_ratelimit_allow(&rl, 0xFFFFFF00u);
    }
    /* now_ms wraps to 0x00000100: elapsed = 0x100 - 0xFFFFFF00 = 0x200 (512). */
    /* 512 milli-tokens < 1000 -> still no whole token. */
    CHECK(control_ratelimit_allow(&rl, 0x00000100u) == 0,
          "wrap: 512 milli after drain grants no token");
    /* Advance another 488 ms to reach 1000 milli total from the wrap point. */
    CHECK(control_ratelimit_allow(&rl, 0x000002E8u) == 1,
          "wrap: accumulated 1000 milli grants one token");
}

/* T5: a partial second must not grant a whole token. */
static void t_partial(void)
{
    control_ratelimit_t rl;
    unsigned i;
    control_ratelimit_init(&rl, 100u);
    /* Drain. */
    for (i = 0; i < CONTROL_RL_CAPACITY; i++) {
        (void)control_ratelimit_allow(&rl, 100u);
    }
    /* +999 ms -> 999 milli-tokens < 1000 -> still drop. */
    CHECK(control_ratelimit_allow(&rl, 100u + 999u) == 0,
          "partial second (999ms) grants no whole token");
    /* +1 more ms (1000 total) -> now exactly one token. */
    CHECK(control_ratelimit_allow(&rl, 100u + 1000u) == 1,
          "reaching 1000ms grants exactly one token");
}

/* T6: sustained human-paced traffic (one query per ~2 s) far past CAP -> all allowed.
 * This is the standing-conversation case the M2b-1 frame-counter clock broke. */
static void t_sustained_human_pace(void)
{
    control_ratelimit_t rl;
    unsigned i;
    int allowed = 0;
    control_ratelimit_init(&rl, 0u);
    for (i = 0; i < 15u; i++) {
        if (control_ratelimit_allow(&rl, i * 2000u)) {
            allowed++;
        }
    }
    CHECK(allowed == 15, "sustained 1 per 2s: all 15 allowed (> CAP)");
}

/* T7: NEGATIVE CONTROL — pins the historical M2b-1 bug shape. Feeding a per-frame
 * counter (tick++) instead of a ms clock refills 1 milli-token per frame against a
 * 1000 milli-token cost, so the bucket dies after exactly CAP frames and the channel
 * is dead until reboot (bare metal: [CTRL-IN-STATS] acc=8 ... rl=2). If this ever
 * reads anything but CAP, the caller's clock has regressed to a frame counter. */
static void t_frame_counter_clock_is_the_bug(void)
{
    control_ratelimit_t rl;
    unsigned i;
    int allowed = 0;
    int denied_after_cap = 1;
    control_ratelimit_init(&rl, 0u);
    for (i = 0; i < 15u; i++) {
        if (control_ratelimit_allow(&rl, i)) {   /* the old tick++ clock */
            allowed++;
            if (i >= CONTROL_RL_CAPACITY) {
                denied_after_cap = 0;
            }
        }
    }
    CHECK(allowed == (int)CONTROL_RL_CAPACITY,
          "frame-counter clock starves at exactly CAP (the M2b-1 bug)");
    CHECK(denied_after_cap, "frame-counter clock never recovers past CAP");
}

/* T8: a burst inside one second still IS limited -> the fix restores refill without
 * removing rate limiting. */
static void t_burst_sub_second_still_limited(void)
{
    control_ratelimit_t rl;
    unsigned i;
    int allowed = 0;
    control_ratelimit_init(&rl, 10000u);
    /* 20 frames spread over 950 ms: refill grants < 1 whole token across the burst. */
    for (i = 0; i < 20u; i++) {
        if (control_ratelimit_allow(&rl, 10000u + i * 50u)) {
            allowed++;
        }
    }
    CHECK(allowed == (int)CONTROL_RL_CAPACITY,
          "sub-second burst still capped at CAP (limiting intact)");
}

int main(void)
{
    t_steady();
    t_burst();
    t_drain_refill_clamp();
    t_wrap();
    t_partial();
    t_sustained_human_pace();
    t_frame_counter_clock_is_the_bug();
    t_burst_sub_second_still_limited();

    if (g_fail == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d TEST(S) FAILED\n", g_fail);
    return 1;
}
