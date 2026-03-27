#include "model_scaling.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define TEST(name) printf("Test %d: %s ... ", pass+fail+1, name)
#define PASS() do { printf("PASS\n"); pass++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); fail++; } while(0)

/* Test 1: Init starts IDLE */
static void test_init_idle(void) {
    TEST("init starts IDLE");
    model_scaler_t s;
    scaler_init(&s);
    if (s.current_state == SCALING_IDLE && s.transitions == 0 && s.idle_counter == 0)
        PASS();
    else
        FAIL("expected IDLE state with 0 transitions and 0 idle_counter");
}

/* Test 2: Low load stays IDLE */
static void test_low_load_stays_idle(void) {
    TEST("low load stays IDLE");
    model_scaler_t s;
    scaler_init(&s);
    scaling_state_t r = scaler_update(&s, 0.1f, 0);
    if (r == SCALING_IDLE && s.transitions == 0)
        PASS();
    else
        FAIL("expected IDLE with 0 transitions");
}

/* Test 3: High load -> ACTIVE */
static void test_high_load_active(void) {
    TEST("high load -> ACTIVE");
    model_scaler_t s;
    scaler_init(&s);
    scaling_state_t r = scaler_update(&s, 0.6f, 0);
    if (r == SCALING_ACTIVE && s.transitions == 1)
        PASS();
    else
        FAIL("expected ACTIVE with 1 transition");
}

/* Test 4: Very high load -> CRITICAL */
static void test_very_high_load_critical(void) {
    TEST("very high load -> CRITICAL");
    model_scaler_t s;
    scaler_init(&s);
    scaling_state_t r = scaler_update(&s, 0.9f, 0);
    if (r == SCALING_CRITICAL && s.transitions == 1)
        PASS();
    else
        FAIL("expected CRITICAL with 1 transition");
}

/* Test 5: Load drop with hysteresis -> back to IDLE */
static void test_hysteresis_back_to_idle(void) {
    TEST("hysteresis de-escalation -> IDLE");
    model_scaler_t s;
    scaler_init(&s);

    /* Escalate to ACTIVE */
    scaler_update(&s, 0.6f, 0);
    if (s.current_state != SCALING_ACTIVE) {
        FAIL("failed to escalate to ACTIVE");
        return;
    }

    /* Low load for 9 updates: should stay ACTIVE (hysteresis not met) */
    for (int i = 0; i < 9; i++) {
        scaler_update(&s, 0.05f, 0);
    }
    if (s.current_state != SCALING_ACTIVE) {
        FAIL("de-escalated too early (before hysteresis threshold)");
        return;
    }

    /* 10th low-load update: hysteresis met, should drop to IDLE */
    scaling_state_t r = scaler_update(&s, 0.05f, 0);
    if (r == SCALING_IDLE && s.current_state == SCALING_IDLE)
        PASS();
    else
        FAIL("expected IDLE after hysteresis period");
}

/* Test 6: force_emergency sets EMERGENCY */
static void test_force_emergency(void) {
    TEST("force_emergency sets EMERGENCY");
    model_scaler_t s;
    scaler_init(&s);
    scaler_force_emergency(&s);
    if (s.current_state == SCALING_EMERGENCY && s.transitions == 1)
        PASS();
    else
        FAIL("expected EMERGENCY with 1 transition");
}

/* Test 7: EMERGENCY is sticky (doesn't auto-recover) */
static void test_emergency_sticky(void) {
    TEST("EMERGENCY sticky (no auto-recover)");
    model_scaler_t s;
    scaler_init(&s);
    scaler_force_emergency(&s);

    /* Try to recover with low load many times */
    for (int i = 0; i < 20; i++) {
        scaler_update(&s, 0.0f, 0);
    }
    if (s.current_state == SCALING_EMERGENCY)
        PASS();
    else
        FAIL("EMERGENCY should not auto-recover");
}

/* Test 8: state_name returns correct strings */
static void test_state_names(void) {
    TEST("state_name returns correct strings");
    int ok = 1;
    if (strcmp(scaler_state_name(SCALING_IDLE), "IDLE") != 0) ok = 0;
    if (strcmp(scaler_state_name(SCALING_ACTIVE), "ACTIVE") != 0) ok = 0;
    if (strcmp(scaler_state_name(SCALING_CRITICAL), "CRITICAL") != 0) ok = 0;
    if (strcmp(scaler_state_name(SCALING_EMERGENCY), "EMERGENCY") != 0) ok = 0;
    if (strcmp(scaler_state_name((scaling_state_t)99), "UNKNOWN") != 0) ok = 0;
    if (ok)
        PASS();
    else
        FAIL("one or more state names incorrect");
}

/* Test 9: Transition counter increments correctly */
static void test_transition_counter(void) {
    TEST("transition counter increments correctly");
    model_scaler_t s;
    scaler_init(&s);

    /* IDLE -> ACTIVE (transition 1) */
    scaler_update(&s, 0.6f, 0);
    if (s.transitions != 1) { FAIL("expected 1 transition after escalation"); return; }

    /* ACTIVE -> CRITICAL (transition 2) */
    scaler_update(&s, 0.9f, 0);
    if (s.transitions != 2) { FAIL("expected 2 transitions after second escalation"); return; }

    /* Stay at CRITICAL with same load (no new transition) */
    scaler_update(&s, 0.85f, 0);
    if (s.transitions != 2) { FAIL("expected 2 transitions (no change)"); return; }

    /* De-escalate with hysteresis: 10 low-load updates -> IDLE (transition 3) */
    for (int i = 0; i < 10; i++) {
        scaler_update(&s, 0.05f, 0);
    }
    if (s.transitions != 3) { FAIL("expected 3 transitions after de-escalation"); return; }

    PASS();
}

int main(void) {
    printf("=== Model Scaling State Machine Tests ===\n\n");

    test_init_idle();
    test_low_load_stays_idle();
    test_high_load_active();
    test_very_high_load_critical();
    test_hysteresis_back_to_idle();
    test_force_emergency();
    test_emergency_sticky();
    test_state_names();
    test_transition_counter();

    printf("\n=== Results: %d PASS, %d FAIL ===\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
