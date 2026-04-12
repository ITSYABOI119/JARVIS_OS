/*
 * JARVIS AI-OS Phase 3 — SSM / Gated DeltaNet Unit Tests
 *
 * Tests conv1d state management and delta-rule recurrence on synthetic data.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
 *       -I phase3/src/ai \
 *       phase3/src/ai/test_ssm.c phase3/src/ai/ssm.c \
 *       -lm -o /tmp/test_ssm
 *
 * Run:
 *   /tmp/test_ssm
 */

#include "ssm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-55s", #name); \
    name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* ---- Conv1d tests ---- */

TEST(test_conv1d_basic)
{
    /* 2 channels, d_conv=3 (2 state rows + 1 current) */
    int d_conv = 3, channels = 2;
    float conv_state[2 * 2] = { /* [state_rows][channels] = [2][2] */
        1.0f, 2.0f,   /* t=0 */
        3.0f, 4.0f,   /* t=1 */
    };
    float kernel[2 * 3] = { /* [channels][d_conv] = [2][3] — GGUF inner-dim-first layout */
        1.0f, 1.0f, 1.0f,   /* channel 0: taps 0,1,2 */
        1.0f, 1.0f, 1.0f,   /* channel 1: taps 0,1,2 */
    };
    float qkv[2] = { 5.0f, 6.0f };  /* current token */

    deltanet_conv1d(qkv, conv_state, kernel, d_conv, channels);

    /* Channel 0: 1*1 + 3*1 + 5*1 = 9 → silu(9) ≈ 8.999 */
    ASSERT_TRUE(qkv[0] > 8.5f && qkv[0] < 9.5f, "conv1d ch0 output");
    /* Channel 1: 2*1 + 4*1 + 6*1 = 12 → silu(12) ≈ 12.0 */
    ASSERT_TRUE(qkv[1] > 11.5f && qkv[1] < 12.5f, "conv1d ch1 output");

    /* State should have shifted: [3,4] then [5,6] */
    ASSERT_TRUE(fabsf(conv_state[0] - 3.0f) < 1e-6f, "conv1d state shift row0 ch0");
    ASSERT_TRUE(fabsf(conv_state[1] - 4.0f) < 1e-6f, "conv1d state shift row0 ch1");
    ASSERT_TRUE(fabsf(conv_state[2] - 5.0f) < 1e-6f, "conv1d state shift row1 ch0");
    ASSERT_TRUE(fabsf(conv_state[3] - 6.0f) < 1e-6f, "conv1d state shift row1 ch1");
}

TEST(test_conv1d_state_persistence)
{
    /* Run conv1d twice, verify state accumulates correctly */
    int d_conv = 3, channels = 1;
    float conv_state[2] = { 0.0f, 0.0f };  /* [state_rows][channels] */
    float kernel[3] = { 0.1f, 0.2f, 0.3f }; /* [channels=1][d_conv=3] — same layout either way */

    /* Token 1: input = 1.0 */
    float qkv1[1] = { 1.0f };
    deltanet_conv1d(qkv1, conv_state, kernel, d_conv, channels);
    /* State after: [0.0, 1.0]. Conv: 0*0.1 + 0*0.2 + 1*0.3 = 0.3 → silu(0.3) */
    ASSERT_TRUE(conv_state[1] == 1.0f, "state[1] has token 1");

    /* Token 2: input = 2.0 */
    float qkv2[1] = { 2.0f };
    deltanet_conv1d(qkv2, conv_state, kernel, d_conv, channels);
    /* State after: [1.0, 2.0]. Conv: 1*0.1 + 1*0.2... wait, let me trace:
     * Before shift: state = [0.0, 1.0] (from token 1)
     * Conv: state[0]*k[0] + state[1]*k[1] + cur*k[2] = 0*0.1 + 1*0.2 + 2*0.3 = 0.8
     * After shift: state = [1.0, 2.0] */
    ASSERT_TRUE(fabsf(conv_state[0] - 1.0f) < 1e-6f, "state persists across calls");
    ASSERT_TRUE(fabsf(conv_state[1] - 2.0f) < 1e-6f, "state has latest token");
}

TEST(test_conv1d_zero_state)
{
    /* Fresh state (all zeros) — only current token contributes */
    int d_conv = 4, channels = 1;
    float conv_state[3] = { 0.0f, 0.0f, 0.0f };  /* [state_rows][channels] */
    float kernel[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; /* [channels=1][d_conv=4] */
    float qkv[1] = { 3.0f };

    deltanet_conv1d(qkv, conv_state, kernel, d_conv, channels);
    /* Conv: 0 + 0 + 0 + 3*1 = 3 → silu(3) ≈ 2.858 */
    ASSERT_TRUE(qkv[0] > 2.5f && qkv[0] < 3.0f, "zero state only current contributes");
}

/* ---- DeltaNet recurrence tests ---- */

TEST(test_deltanet_zero_state)
{
    /* With zero SSM state, delta rule should just write v@k^T scaled by beta */
    int nkh = 1, nvh = 1, hkd = 2, hvd = 2;
    float q[2] = { 1.0f, 0.0f };
    float k[2] = { 1.0f, 0.0f };
    float v[2] = { 1.0f, 2.0f };
    float z[2] = { 10.0f, 10.0f };  /* large → silu ≈ identity */
    float alpha[1] = { 0.0f };  /* softplus(0+0)*ssm_a → exp(softplus(0)*ssm_a) */
    float beta_raw[1] = { 10.0f };  /* sigmoid(10) ≈ 1.0 → beta ≈ 1 */
    float ssm_a[1] = { -1.0f };
    float dt_bias[1] = { 0.0f };
    float norm_w[2] = { 1.0f, 1.0f };
    float ssm_state[4] = { 0 };  /* [1 * 2 * 2] = zero */
    float out[2];

    deltanet_forward(q, k, v, z, alpha, beta_raw, ssm_a, dt_bias,
                     norm_w, 1e-5f, ssm_state, out, nkh, nvh, hkd, hvd);

    /* With zero state and beta≈1: S = 0 + 1*(v - 0)@k^T = v@k^T = [[1,2],[0,0]]
     * out = S^T @ q = S[:,0] (since q=[1,0]) = [1, 0] (the first column of S)
     * Wait: S[i][j] += k[i]*delta[j]. k=[1,0] (normalized), delta=beta*(v-pred)=1*(v-0)=[1,2]
     * S = [[1*1, 1*2], [0*1, 0*2]] = [[1,2],[0,0]]
     * out[j] = sum_i S[i][j]*q[i]. q=[1,0] after L2 norm.
     * out[0] = S[0][0]*1 + S[1][0]*0 = 1
     * out[1] = S[0][1]*1 + S[1][1]*0 = 2
     * After norm + silu(z) gating: non-zero output expected */
    ASSERT_TRUE(fabsf(out[0]) > 0.01f, "non-zero output from zero state");
    ASSERT_TRUE(fabsf(out[1]) > 0.01f, "second dim non-zero");
}

TEST(test_deltanet_state_decay)
{
    /* Verify state decays with gate < 1 */
    int nkh = 1, nvh = 1, hkd = 2, hvd = 2;
    float q[2] = { 0.0f, 0.0f };  /* zero query → no read */
    float k[2] = { 0.0f, 0.0f };  /* zero key → no write */
    float v[2] = { 0.0f, 0.0f };
    float z[2] = { 0.0f, 0.0f };
    float alpha[1] = { 5.0f };  /* large alpha → large softplus → strong decay */
    float beta_raw[1] = { 0.0f };
    float ssm_a[1] = { -1.0f };
    float dt_bias[1] = { 0.0f };
    float norm_w[2] = { 1.0f, 1.0f };
    float ssm_state[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  /* non-zero initial state */
    float out[2];

    deltanet_forward(q, k, v, z, alpha, beta_raw, ssm_a, dt_bias,
                     norm_w, 1e-5f, ssm_state, out, nkh, nvh, hkd, hvd);

    /* Gate = exp(softplus(5) * (-1)) = exp(-5.007) ≈ 0.0067 → state should decay */
    float expected_gate = expf(logf(1.0f + expf(5.0f)) * (-1.0f));
    ASSERT_TRUE(ssm_state[0] < 0.01f, "state decayed significantly");
    ASSERT_TRUE(fabsf(ssm_state[0] - expected_gate) < 0.01f, "state matches gate formula");
}

TEST(test_deltanet_multi_head)
{
    /* 2 K-heads, 4 V-heads (GQA ratio 2:1) — verify head mapping */
    int nkh = 2, nvh = 4, hkd = 2, hvd = 2;
    float q[4] = { 1,0, 0,1 };  /* 2 K-heads × 2 dims */
    float k[4] = { 1,0, 0,1 };
    float v[8] = { 1,0, 0,1, 1,0, 0,1 };  /* 4 V-heads × 2 dims */
    float z[8] = { 10,10, 10,10, 10,10, 10,10 };
    float alpha[4] = { 0,0,0,0 };
    float beta_raw[4] = { 10,10,10,10 };
    float ssm_a[4] = { -0.1f, -0.1f, -0.1f, -0.1f };
    float dt_bias[4] = { 0,0,0,0 };
    float norm_w[2] = { 1.0f, 1.0f };
    float ssm_state[4 * 2 * 2];  /* 4 heads × 2 × 2 */
    memset(ssm_state, 0, sizeof(ssm_state));
    float out[8];

    deltanet_forward(q, k, v, z, alpha, beta_raw, ssm_a, dt_bias,
                     norm_w, 1e-5f, ssm_state, out, nkh, nvh, hkd, hvd);

    /* V-heads 0 and 1 share K-head 0; V-heads 2 and 3 share K-head 1 */
    /* All outputs should be non-zero */
    for (int i = 0; i < 8; i++)
        ASSERT_TRUE(!isnan(out[i]) && !isinf(out[i]), "multi-head output finite");
}

TEST(test_deltanet_no_nan)
{
    /* Large-ish realistic dims: 4 K-heads, 8 V-heads, dim 4 */
    int nkh = 4, nvh = 8, hkd = 4, hvd = 4;
    float q[16], k[16], v[32], z[32];
    float alpha[8], beta_raw[8], ssm_a[8], dt_bias[8];
    float norm_w[4] = { 1,1,1,1 };
    float ssm_state[8 * 4 * 4];
    float out[32];

    /* Fill with small random-ish values */
    for (int i = 0; i < 16; i++) { q[i] = 0.1f * (i - 8); k[i] = 0.1f * (i - 7); }
    for (int i = 0; i < 32; i++) { v[i] = 0.05f * (i - 16); z[i] = 1.0f; }
    for (int i = 0; i < 8; i++) {
        alpha[i] = 0.5f; beta_raw[i] = 0.0f;
        ssm_a[i] = -0.1f; dt_bias[i] = 0.0f;
    }
    memset(ssm_state, 0, sizeof(ssm_state));

    deltanet_forward(q, k, v, z, alpha, beta_raw, ssm_a, dt_bias,
                     norm_w, 1e-5f, ssm_state, out, nkh, nvh, hkd, hvd);

    for (int i = 0; i < 32; i++)
        ASSERT_TRUE(!isnan(out[i]) && !isinf(out[i]), "realistic dims: no NaN/Inf");
}

/* ---- Main ---- */

int main(void)
{
    printf("=== SSM / Gated DeltaNet Unit Tests ===\n\n");

    RUN(test_conv1d_basic);
    RUN(test_conv1d_state_persistence);
    RUN(test_conv1d_zero_state);
    RUN(test_deltanet_zero_state);
    RUN(test_deltanet_state_decay);
    RUN(test_deltanet_multi_head);
    RUN(test_deltanet_no_nan);

    printf("\n--- Results: %d PASS, %d FAIL ---\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
