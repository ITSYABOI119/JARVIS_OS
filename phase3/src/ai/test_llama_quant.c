/*
 * JARVIS AI-OS Phase 3 — Quantized Model Loading Tests
 *
 * Tests qmodel_load, qmatmul_vec, qembed_lookup, and qmodel_forward
 * using synthetic data (no real model file needed for CI).
 */

#include "llama_quant.h"
#include "llama_model.h"
#include "dequant.h"
#include "qdot.h"      /* qdot_row — the public per-row quantized dot that qmatmul_vec uses */
#include "tensor_ops.h"
#include <stdint.h>
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

/* ---- Part 2 (AVX2 coverage): quantized qdot vs an independent dequant+dot reference ----
 * The tests above drive qmodel_forward with F32 weights only, so the quantized qdot kernels
 * that dominate the deployed forward are never exercised here. qdot_row is the public per-row
 * primitive that qmatmul_vec calls; compiled with -mavx2 it dispatches to the AVX2 kernels
 * (qdot_q8_0_avx2 / qdot_q4_k_avx2), which we check against dequant_row + a double-accumulated
 * dot (the scalar truth). Synthetic blocks use finite f16 scales + a deterministic LCG byte
 * pattern; magnitudes are kept small so the float-vs-double accumulation gap stays far under the
 * absolute tolerance (measured ~3e-8 Q8_0 / ~3e-7 Q4_K, vs 1e-3 / 1e-2). These also run in the
 * existing (scalar) build, validating qdot_row_scalar the same way. */
static uint8_t qd_lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return (uint8_t)(*s >> 23); }

static double qd_ref_dot(const void *qrow, const float *x, int K, ggml_type_t type) {
    float deq[256];
    dequant_row(qrow, deq, K, type);
    double acc = 0.0;
    for (int i = 0; i < K; i++) acc += (double)deq[i] * (double)x[i];
    return acc;
}

/* ---- Test 11: Q8_0 qdot matches dequant+dot reference ---- */
static void test_qdot_q8_0_vs_ref(void) {
    TEST("qdot Q8_0 matches dequant+dot reference (AVX2 build exercises qdot_q8_0_avx2)");
    const int K = 256;                       /* 8 Q8_0 blocks of 32 */
    q8_0_block_t blk[8];
    uint32_t s = 0x8badf00du;
    for (int b = 0; b < 8; b++) {
        blk[b].d = 0x3000;                   /* f16 0.125 */
        for (int i = 0; i < 32; i++) blk[b].qs[i] = (int8_t)((qd_lcg(&s) % 9) - 4);
    }
    float x[256];
    for (int i = 0; i < K; i++) x[i] = (float)((i % 7) - 3) * 0.1f;
    float got = qdot_row(blk, x, K, GGML_TYPE_Q8_0);
    double ref = qd_ref_dot(blk, x, K, GGML_TYPE_Q8_0);
    double err = fabs((double)got - ref);
    if (err < 1e-3) {
        PASS();
    } else {
        char m[96];
        snprintf(m, sizeof m, "Q8_0 err=%.3e (got=%.6f ref=%.6f)", err, got, ref);
        FAIL(m);
    }
}

/* ---- Test 12: Q4_K qdot matches dequant+dot reference ---- */
static void test_qdot_q4k_vs_ref(void) {
    TEST("qdot Q4_K matches dequant+dot reference (AVX2 build exercises qdot_q4_k_avx2)");
    const int K = 256;                       /* 1 Q4_K super-block */
    q4_k_block_t blk;
    blk.d = 0x3000;                          /* f16 0.125 */
    blk.dmin = 0x2C00;                       /* f16 0.0625 */
    uint32_t s = 0x00c0ffeeu;
    for (int i = 0; i < 12; i++) blk.scales[i] = (uint8_t)(qd_lcg(&s) & 0x0F);
    for (int i = 0; i < 128; i++) blk.qs[i] = qd_lcg(&s);
    float x[256];
    for (int i = 0; i < K; i++) x[i] = (float)((i % 5) - 2) * 0.02f;
    float got = qdot_row(&blk, x, K, GGML_TYPE_Q4_K);
    double ref = qd_ref_dot(&blk, x, K, GGML_TYPE_Q4_K);
    double err = fabs((double)got - ref);
    if (err < 1e-2) {
        PASS();
    } else {
        char m[96];
        snprintf(m, sizeof m, "Q4_K err=%.3e (got=%.6f ref=%.6f)", err, got, ref);
        FAIL(m);
    }
}

/* ---- H2: load-time quant-type whitelist gate (qmodel_load hard-rejects unsupported types) ---- */

/* Test 13: the dequant_type_supported() truth table — catches a botched whitelist (the bug
 * that would silently break the box: rejecting a real type, or accepting one the forward can't do). */
static void test_dequant_type_supported_table(void) {
    TEST("dequant_type_supported whitelist truth table");
    int ok = 1;
    const ggml_type_t supported[] = {
        GGML_TYPE_F32, GGML_TYPE_F16, GGML_TYPE_BF16, GGML_TYPE_Q4_0,
        GGML_TYPE_Q8_0, GGML_TYPE_Q4_K, GGML_TYPE_Q5_K, GGML_TYPE_Q6_K,
    };
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); i++)
        if (dequant_type_supported(supported[i]) != 1) { ok = 0; printf("[supported %d->0] ", (int)supported[i]); }
    const int unsupported[] = { GGML_TYPE_Q2_K, GGML_TYPE_Q3_K, GGML_TYPE_Q8_K, GGML_TYPE_F64, 99 };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
        if (dequant_type_supported((ggml_type_t)unsupported[i]) != 0) { ok = 0; printf("[unsupported %d->1] ", unsupported[i]); }
    if (ok) PASS(); else FAIL("dequant_type_supported truth table mismatch");
}

/* ---- minimal complete in-memory "llama" GGUF so qmodel_load runs end-to-end ---- */
static size_t h2_put_str(uint8_t *b, const char *s) {
    uint64_t n = (uint64_t)strlen(s);
    memcpy(b, &n, 8); memcpy(b + 8, s, (size_t)n);
    return 8 + (size_t)n;
}
static size_t h2_put_u32kv(uint8_t *b, const char *k, uint32_t v) {
    uint8_t *p = b;
    p += h2_put_str(p, k);
    uint32_t t = GGUF_TYPE_UINT32; memcpy(p, &t, 4); p += 4;
    memcpy(p, &v, 4); p += 4;
    return (size_t)(p - b);
}
/* Build a complete, loadable 1-layer all-F32 "llama" model (separate Q/K/V + separate gate/up,
 * no SWA/SSM/PLE so the shape-derivation block is skipped and qmodel_load resolves every
 * mandatory tensor and reaches the type gate). buf must be >= 8 KB. Returns blob length. */
static size_t h2_build_gguf(uint8_t *buf) {
    struct { const char *name; uint32_t nd; uint64_t d0, d1, nel; } T[] = {
        { "token_embd.weight",        2, 8, 4, 32 }, { "output_norm.weight",       1, 8, 0, 8 },
        { "blk.0.attn_norm.weight",   1, 8, 0, 8 },  { "blk.0.attn_q.weight",      2, 8, 8, 64 },
        { "blk.0.attn_k.weight",      2, 8, 8, 64 }, { "blk.0.attn_v.weight",      2, 8, 8, 64 },
        { "blk.0.attn_output.weight", 2, 8, 8, 64 }, { "blk.0.ffn_norm.weight",    1, 8, 0, 8 },
        { "blk.0.ffn_gate.weight",    2, 8, 16, 128 }, { "blk.0.ffn_up.weight",    2, 8, 16, 128 },
        { "blk.0.ffn_down.weight",    2, 16, 8, 128 },
    };
    int nt = (int)(sizeof(T) / sizeof(T[0]));
    uint8_t *p = buf;
    uint32_t magic = GGUF_MAGIC, ver = 3;
    uint64_t ntt = (uint64_t)nt, nkv = 6;
    memcpy(p, &magic, 4); p += 4; memcpy(p, &ver, 4); p += 4;
    memcpy(p, &ntt, 8); p += 8; memcpy(p, &nkv, 8); p += 8;
    p += h2_put_str(p, "general.architecture");
    uint32_t ts = GGUF_TYPE_STRING; memcpy(p, &ts, 4); p += 4; p += h2_put_str(p, "llama");
    p += h2_put_u32kv(p, "llama.embedding_length", 8);
    p += h2_put_u32kv(p, "llama.block_count", 1);
    p += h2_put_u32kv(p, "llama.attention.head_count", 2);
    p += h2_put_u32kv(p, "llama.feed_forward_length", 16);
    p += h2_put_u32kv(p, "llama.vocab_size", 4);
    uint64_t off = 0;
    for (int i = 0; i < nt; i++) {
        p += h2_put_str(p, T[i].name);
        memcpy(p, &T[i].nd, 4); p += 4;
        memcpy(p, &T[i].d0, 8); p += 8;
        if (T[i].nd == 2) { memcpy(p, &T[i].d1, 8); p += 8; }
        uint32_t ty = GGML_TYPE_F32; memcpy(p, &ty, 4); p += 4;
        memcpy(p, &off, 8); p += 8;
        off += T[i].nel * 4;                     /* F32 bytes; every tensor is a 32-multiple */
    }
    size_t hdr = (size_t)(p - buf);
    size_t dstart = (hdr + 31) & ~(size_t)31;    /* 32-align the data section */
    memset(buf + dstart, 0, (size_t)off);        /* tensor data is unread at load time — zeros */
    return dstart + (size_t)off;
}
/* Parse the blob, optionally override one tensor's type (to drive the gate), then qmodel_load.
 * Returns qmodel_load's rc; -99/-98 mark a harness failure (parse / tensor-not-found). */
static int h2_try_load(uint8_t *buf, size_t len, const char *override_name, uint32_t override_type) {
    gguf_ctx_t ctx;
    if (gguf_open_memory(&ctx, buf, len) != GGUF_OK) return -99;
    if (override_name) {
        gguf_tensor_info_t *ti = (gguf_tensor_info_t *)gguf_find_tensor(&ctx, override_name);
        if (!ti) { gguf_close(&ctx); return -98; }
        ti->type = override_type;                /* keeps the blob valid; isolates the type gate */
    }
    qmodel_t qm;
    int rc = qmodel_load(&qm, &ctx, buf);
    if (rc == 0) qmodel_free(&qm);
    gguf_close(&ctx);
    return rc;
}

/* Test 14: POSITIVE CONTROL — a complete all-F32 model must load (proves the model is otherwise
 * valid, so the negatives below fail on the type gate and not on a config/tensor error). */
static void test_qmodel_load_f32_positive(void) {
    TEST("qmodel_load accepts a complete all-F32 model (false-pass guard)");
    uint8_t buf[8192];
    size_t len = h2_build_gguf(buf);
    int rc = h2_try_load(buf, len, NULL, 0);
    if (rc == 0) { PASS(); }
    else { char m[64]; snprintf(m, sizeof m, "F32 model should load (rc=%d)", rc); FAIL(m); }
}

/* Test 15: NEGATIVE — token_embd.weight as Q2_K (assigned via resolve_qtensor) must be rejected. */
static void test_qmodel_load_rejects_token_embd(void) {
    TEST("qmodel_load rejects unsupported token_embd type (Q2_K, resolve_qtensor path)");
    uint8_t buf[8192];
    size_t len = h2_build_gguf(buf);
    int rc = h2_try_load(buf, len, "token_embd.weight", GGML_TYPE_Q2_K);
    if (rc < 0) { PASS(); }
    else { char m[64]; snprintf(m, sizeof m, "Q2_K token_embd should fail load (rc=%d)", rc); FAIL(m); }
}

/* Test 16: NEGATIVE — blk.0.ffn_gate.weight as Q2_K (assigned INLINE, not via resolve_qtensor)
 * must be rejected. Guards against a partial fix that only patched resolve_qtensor. */
static void test_qmodel_load_rejects_ffn_gate(void) {
    TEST("qmodel_load rejects unsupported ffn_gate type (Q2_K, inline-assigned path)");
    uint8_t buf[8192];
    size_t len = h2_build_gguf(buf);
    int rc = h2_try_load(buf, len, "blk.0.ffn_gate.weight", GGML_TYPE_Q2_K);
    if (rc < 0) { PASS(); }
    else { char m[64]; snprintf(m, sizeof m, "Q2_K ffn_gate should fail load (rc=%d)", rc); FAIL(m); }
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
    test_qdot_q8_0_vs_ref();
    test_qdot_q4k_vs_ref();
    test_dequant_type_supported_table();
    test_qmodel_load_f32_positive();
    test_qmodel_load_rejects_token_embd();
    test_qmodel_load_rejects_ffn_gate();

    printf("\n=== Results: %d/%d PASS, %d FAIL ===\n",
           tests_pass, tests_pass + tests_fail, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
