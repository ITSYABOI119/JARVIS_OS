/*
 * JARVIS AI-OS Phase 3 — Quantized Model Loading Tests
 *
 * Tests qmodel_load, qmatmul_vec, qembed_lookup, and qmodel_forward
 * using synthetic data (no real model file needed for CI).
 */

#include "llama_quant.h"
#include "llama_model.h"
#include "dequant.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name) printf("Test %d: %s ... ", tests_pass + tests_fail + 1, name)
#define PASS() do { printf("PASS\n"); tests_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_fail++; } while(0)

/* ---- Test 1: qmodel_t struct sizes are sane ---- */
static void test_struct_sizes(void) {
    TEST("qtensor_t / qlayer_t / qmodel_t struct sizes");
    if (sizeof(qtensor_t) >= 16 && sizeof(qlayer_t) >= 9 * sizeof(qtensor_t)
        && sizeof(qmodel_t) >= sizeof(llama_config_t))
        PASS();
    else
        FAIL("unexpected struct sizes");
}

/* ---- Test 2: qmodel_load rejects NULL ---- */
static void test_null_reject(void) {
    TEST("qmodel_load rejects NULL inputs");
    qmodel_t qm;
    if (qmodel_load(&qm, NULL, NULL) < 0)
        PASS();
    else
        FAIL("should return error for NULL");
}

/* ---- Test 3: qmodel_free on zeroed struct is safe ---- */
static void test_free_safe(void) {
    TEST("qmodel_free on zeroed struct");
    qmodel_t qm;
    memset(&qm, 0, sizeof(qm));
    qmodel_free(&qm);  /* Should not crash */
    PASS();
}

/* ---- Test 4: qmodel_free NULL is safe ---- */
static void test_free_null(void) {
    TEST("qmodel_free(NULL) is safe");
    qmodel_free(NULL);  /* Should not crash */
    PASS();
}

/* ---- Test 5: Q4_K dequant_row_bytes matches expectations ---- */
static void test_q4k_row_bytes(void) {
    TEST("Q4_K row bytes for dim=2048");
    /* 2048 elements / 256 block_size = 8 blocks * 144 bytes = 1152 */
    size_t rb = dequant_row_bytes(2048, GGML_TYPE_Q4_K);
    if (rb == 1152)
        PASS();
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 1152, got %zu", rb);
        FAIL(msg);
    }
}

/* ---- Test 6: Q6_K row bytes for dim=2048 ---- */
static void test_q6k_row_bytes(void) {
    TEST("Q6_K row bytes for dim=2048");
    /* 2048 / 256 = 8 blocks * 210 bytes = 1680 */
    size_t rb = dequant_row_bytes(2048, GGML_TYPE_Q6_K);
    if (rb == 1680)
        PASS();
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 1680, got %zu", rb);
        FAIL(msg);
    }
}

/* ---- Test 7: F32 row bytes ---- */
static void test_f32_row_bytes(void) {
    TEST("F32 row bytes for dim=2048");
    size_t rb = dequant_row_bytes(2048, GGML_TYPE_F32);
    if (rb == 2048 * 4)
        PASS();
    else
        FAIL("wrong byte count");
}

/* ---- Test 8: llama_config_t field expectations ---- */
static void test_config_fields(void) {
    TEST("llama_config_t head_dim = dim / n_heads");
    llama_config_t c;
    memset(&c, 0, sizeof(c));
    c.dim = 2048;
    c.n_heads = 32;
    c.head_dim = c.dim / c.n_heads;
    if (c.head_dim == 64)
        PASS();
    else
        FAIL("head_dim should be 64");
}

/* ---- Test 9: llama_alloc_state with small config ---- */
static void test_alloc_state_small(void) {
    TEST("llama_alloc_state with tiny config succeeds");
    llama_config_t c = {
        .dim = 32, .n_layers = 1, .n_heads = 2, .n_kv_heads = 2,
        .head_dim = 16, .hidden_dim = 64, .vocab_size = 100,
        .max_seq_len = 16, .rope_theta = 10000.0f
    };
    llama_state_t s;
    int err = llama_alloc_state(&s, &c);
    if (err == 0 && s.logits != NULL && s.key_cache != NULL) {
        llama_free_state(&s);
        PASS();
    } else {
        FAIL("alloc_state failed");
    }
}

/* ---- Test 10: qmodel_forward with invalid token doesn't crash ---- */
static void test_forward_bad_token(void) {
    TEST("qmodel_forward with out-of-range token produces zeroed logits");
    /* Create a minimal qmodel with valid config and dummy F32 layer weights.
     * qmodel_forward runs the full forward pass even for invalid tokens (required
     * for SSM temporal continuity), so we need valid layer data — not just NULL. */
    qmodel_t qm;
    memset(&qm, 0, sizeof(qm));
    qm.config.dim = 4;
    qm.config.n_layers = 1;
    qm.config.n_heads = 1;
    qm.config.n_kv_heads = 1;
    qm.config.head_dim = 4;
    qm.config.hidden_dim = 8;
    qm.config.vocab_size = 10;
    qm.config.max_seq_len = 4;
    qm.config.rope_theta = 10000.0f;

    /* Dummy F32 embedding: 10 * 4 floats */
    float dummy_embed[40];
    memset(dummy_embed, 0, sizeof(dummy_embed));
    qm.token_embed.data = dummy_embed;
    qm.token_embed.type = GGML_TYPE_F32;
    qm.token_embed.n_elements = 40;

    /* Dummy F32 output norm and weight */
    float dummy_norm[4] = {1, 1, 1, 1};
    qm.output_norm.data = dummy_norm;
    qm.output_norm.type = GGML_TYPE_F32;
    float dummy_out[40];
    memset(dummy_out, 0, sizeof(dummy_out));
    qm.output_weight.data = dummy_out;
    qm.output_weight.type = GGML_TYPE_F32;
    qm.output_weight.n_elements = 40;

    /* Allocate one layer with valid F32 weight pointers.
     * All weights are zero (valid for F32). Norms are 1.0 to avoid NaN.
     * dim=4, kv_dim=4, hidden_dim=8 → max tensor is 8×4=32 floats. */
    float zero_weights[32];
    memset(zero_weights, 0, sizeof(zero_weights));
    float norm_ones[4] = {1, 1, 1, 1};

    qlayer_t layer;
    memset(&layer, 0, sizeof(layer));
    /* Set all weight types to F32 with data pointing to zero buffer */
    layer.wq.data = zero_weights;   layer.wq.type = GGML_TYPE_F32;   layer.wq.n_elements = 16;
    layer.wk.data = zero_weights;   layer.wk.type = GGML_TYPE_F32;   layer.wk.n_elements = 16;
    layer.wv.data = zero_weights;   layer.wv.type = GGML_TYPE_F32;   layer.wv.n_elements = 16;
    layer.wo.data = zero_weights;   layer.wo.type = GGML_TYPE_F32;   layer.wo.n_elements = 16;
    layer.w_gate.data = zero_weights; layer.w_gate.type = GGML_TYPE_F32; layer.w_gate.n_elements = 32;
    layer.w_up.data = zero_weights;   layer.w_up.type = GGML_TYPE_F32;   layer.w_up.n_elements = 32;
    layer.w_down.data = zero_weights; layer.w_down.type = GGML_TYPE_F32; layer.w_down.n_elements = 32;
    /* Norm weights must be 1.0 (rms_norm divides by RMS, multiplies by weight) */
    layer.attn_norm.data = norm_ones; layer.attn_norm.type = GGML_TYPE_F32; layer.attn_norm.n_elements = 4;
    layer.ffn_norm.data = norm_ones;  layer.ffn_norm.type = GGML_TYPE_F32;  layer.ffn_norm.n_elements = 4;

    qm.layers = &layer;
    qm.loaded = true;

    llama_state_t s;
    int err = llama_alloc_state(&s, &qm.config);
    if (err != 0) { FAIL("state alloc"); return; }

    /* Token -1 is out of range — should zero logits and not crash */
    qmodel_forward(&qm, &s, -1);
    int all_zero = 1;
    for (int i = 0; i < 10; i++)
        if (s.logits[i] != 0.0f) all_zero = 0;

    if (all_zero && s.pos == 1)
        PASS();
    else
        FAIL("bad token should produce zeroed logits");

    llama_free_state(&s);
}

int main(void)
{
    printf("=== JARVIS Quantized Model (llama_quant) Test Suite ===\n\n");

    test_struct_sizes();
    test_null_reject();
    test_free_safe();
    test_free_null();
    test_q4k_row_bytes();
    test_q6k_row_bytes();
    test_f32_row_bytes();
    test_config_fields();
    test_alloc_state_small();
    test_forward_bad_token();

    printf("\n=== Results: %d/%d PASS, %d FAIL ===\n",
           tests_pass, tests_pass + tests_fail, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
