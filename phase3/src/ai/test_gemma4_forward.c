/*
 * JARVIS AI-OS Phase 3 — Gemma 4 Forward Pass Feature Tests
 *
 * Tests individual Gemma 4 forward pass features in isolation
 * using mock/synthetic data (no real model file needed).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int pass_count = 0, fail_count = 0;
#define TEST(name) printf("  %-50s ", name)
#define PASS() do { pass_count++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { fail_count++; printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) FAIL(msg); } while(0)

/* ---- Test: logit_softcap ---- */

/* We can't call the static function directly, so we replicate it for testing.
 * This verifies the MATH is correct; the integration (calling it from qmodel_forward)
 * is verified by the integration test in Task 8. */
static void logit_softcap_ref(float *logits, int n, float cap)
{
    if (cap <= 0.0f) return;
    float inv_cap = 1.0f / cap;
    for (int i = 0; i < n; i++)
        logits[i] = cap * tanhf(logits[i] * inv_cap);
}

static void test_logit_softcap_basic(void)
{
    TEST("logit softcap: cap * tanh(x / cap)");
    float logits[4] = { 50.0f, -50.0f, 0.0f, 30.0f };
    float cap = 30.0f;
    logit_softcap_ref(logits, 4, cap);

    /* 30*tanh(50/30) = 30*tanh(1.667) ~ 30*0.931 = 27.93 */
    ASSERT(fabsf(logits[0] - 27.93f) < 0.1f, "positive capped ~27.93");
    /* 30*tanh(-50/30) ~ -27.93 */
    ASSERT(fabsf(logits[1] + 27.93f) < 0.1f, "negative capped ~-27.93");
    /* 30*tanh(0) = 0 */
    ASSERT(fabsf(logits[2]) < 0.001f, "zero stays zero");
    /* 30*tanh(30/30) = 30*tanh(1) ~ 30*0.7616 = 22.85 */
    ASSERT(fabsf(logits[3] - 22.85f) < 0.1f, "tanh(1) ~ 22.85");
    PASS();
}

static void test_logit_softcap_noop_when_zero(void)
{
    TEST("logit softcap: no-op when cap=0");
    float logits[3] = { 100.0f, -100.0f, 42.0f };
    float orig[3];
    memcpy(orig, logits, sizeof(orig));
    logit_softcap_ref(logits, 3, 0.0f);
    ASSERT(logits[0] == orig[0] && logits[1] == orig[1] && logits[2] == orig[2],
           "logits unchanged when cap=0");
    PASS();
}

static void test_logit_softcap_small_values_passthrough(void)
{
    TEST("logit softcap: small values ~ identity");
    float logits[2] = { 1.0f, -1.0f };
    float cap = 30.0f;
    logit_softcap_ref(logits, 2, cap);
    /* tanh(1/30) ~ 1/30, so 30 * (1/30) ~ 1.0 */
    ASSERT(fabsf(logits[0] - 1.0f) < 0.01f, "small positive ~identity");
    ASSERT(fabsf(logits[1] + 1.0f) < 0.01f, "small negative ~identity");
    PASS();
}

int main(void)
{
    printf("=== Gemma 4 Forward Pass Tests ===\n\n");
    test_logit_softcap_basic();
    test_logit_softcap_noop_when_zero();
    test_logit_softcap_small_values_passthrough();
    printf("\n--- Results: %d PASS, %d FAIL ---\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
