/*
 * JARVIS AI-OS Phase 3 -- Tests for qmodel_tq_state_t alloc/free
 *
 * Test 1: test_alloc_free     -- small config, verify fields + tq_states[0].d
 * Test 2: test_memory_savings -- Llama 1B config, verify >3x compression ratio
 */

#include "qmodel_tq_forward.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond, msg) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s\n", msg); \
            g_fail++; \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test 1: alloc + free with a small config                            */
/* ------------------------------------------------------------------ */
static void test_alloc_free(void)
{
    printf("test_alloc_free:\n");

    llama_config_t cfg = {
        .dim        = 64,
        .n_layers   = 4,
        .n_heads    = 8,
        .n_kv_heads = 8,
        .head_dim   = 8,
        .hidden_dim = 128,
        .vocab_size = 100,
        .max_seq_len = 32,
        .rope_theta  = 10000.0f
    };

    qmodel_tq_state_t state;
    memset(&state, 0xff, sizeof(state)); /* poison to catch missing inits */

    int rc = qmodel_tq_alloc_state(&state, &cfg, 3, 3, 0x1234ULL);
    EXPECT(rc == 0, "alloc returns 0");

    /* Activation buffers non-NULL */
    EXPECT(state.x      != NULL, "x allocated");
    EXPECT(state.xb     != NULL, "xb allocated");
    EXPECT(state.xb2    != NULL, "xb2 allocated");
    EXPECT(state.q      != NULL, "q allocated");
    EXPECT(state.k      != NULL, "k allocated");
    EXPECT(state.v      != NULL, "v allocated");
    EXPECT(state.att    != NULL, "att allocated");
    EXPECT(state.hb     != NULL, "hb allocated");
    EXPECT(state.hb2    != NULL, "hb2 allocated");
    EXPECT(state.logits != NULL, "logits allocated");

    /* Compressed KV cache non-NULL */
    EXPECT(state.key_cache  != NULL, "key_cache allocated");
    EXPECT(state.val_cache  != NULL, "val_cache allocated");

    /* tq_states dynamically allocated (NOT a fixed array field offset) */
    EXPECT(state.tq_states  != NULL, "tq_states allocated");

    /* Metadata fields */
    EXPECT(state.n_layers   == 4,  "n_layers == 4");
    EXPECT(state.n_kv_heads == 8,  "n_kv_heads == 8");
    EXPECT(state.max_seq_len == 32, "max_seq_len == 32");
    EXPECT(state.pos        == 0,  "pos == 0");
    EXPECT(state.key_bits   == 3,  "key_bits == 3");
    EXPECT(state.val_bits   == 3,  "val_bits == 3");

    /* tq_states[0] initialized with correct head_dim */
    EXPECT(state.tq_states[0].d == cfg.head_dim,
           "tq_states[0].d == head_dim");

    /* tq_states is a heap pointer -- must differ from &state */
    EXPECT((void *)state.tq_states != (void *)&state,
           "tq_states is heap (not embedded in struct)");

    /* Free and verify cleanup */
    qmodel_tq_free_state(&state);

    EXPECT(state.x         == NULL, "x NULL after free");
    EXPECT(state.key_cache == NULL, "key_cache NULL after free");
    EXPECT(state.val_cache == NULL, "val_cache NULL after free");
    EXPECT(state.tq_states == NULL, "tq_states NULL after free");
    EXPECT(state.n_layers  == 0,   "n_layers 0 after free");
}

/* ------------------------------------------------------------------ */
/* Test 2: memory savings for a Llama 1B-like config                   */
/* ------------------------------------------------------------------ */
static void test_memory_savings(void)
{
    printf("test_memory_savings:\n");

    /* Llama 3.2 1B approximate config */
    llama_config_t cfg = {
        .dim        = 2048,
        .n_layers   = 16,
        .n_heads    = 32,
        .n_kv_heads = 8,
        .head_dim   = 64,
        .hidden_dim = 8192,
        .vocab_size = 128256,
        .max_seq_len = 512,
        .rope_theta  = 500000.0f
    };

    qmodel_tq_state_t state;
    int rc = qmodel_tq_alloc_state(&state, &cfg, 3, 3, 0xABCD1234ULL);
    EXPECT(rc == 0, "alloc Llama 1B config returns 0");

    if (rc != 0) {
        printf("  (skipping memory ratio check — alloc failed)\n");
        return;
    }

    /* Compute sizes */
    size_t cache_entries = (size_t)cfg.n_layers *
                           (size_t)cfg.max_seq_len *
                           (size_t)cfg.n_kv_heads;

    /* F32 KV: 2 caches * entries * head_dim * 4 bytes */
    size_t f32_kv = cache_entries * (size_t)cfg.head_dim * 2 * sizeof(float);

    /* TQ KV: compressed struct sizes */
    size_t tq_kv = cache_entries * (sizeof(tq_ckey_t) + sizeof(tq_cval_t));

    double ratio = (tq_kv > 0) ? (double)f32_kv / (double)tq_kv : 0.0;

    printf("  F32 KV cache:  %zu bytes (%.1f MB)\n",
           f32_kv, (double)f32_kv / (1024.0 * 1024.0));
    printf("  TQ  KV cache:  %zu bytes (%.1f KB)\n",
           tq_kv, (double)tq_kv / 1024.0);
    printf("  Compression ratio: %.2fx\n", ratio);

    EXPECT(f32_kv > tq_kv, "F32 KV > TQ KV (compression achieved)");
    EXPECT(ratio > 3.0, "compression ratio > 3x");

    qmodel_tq_free_state(&state);
    EXPECT(state.tq_states == NULL, "tq_states NULL after Llama 1B free");
}

/* ------------------------------------------------------------------ */
/* Test 3: forward pass smoke test — no crash, non-zero logits         */
/* ------------------------------------------------------------------ */
static int test_forward_no_crash(void) {
    printf("  test_forward_no_crash...");
    llama_config_t config = {
        .dim = 16, .n_layers = 1, .n_heads = 2, .n_kv_heads = 2,
        .head_dim = 8, .hidden_dim = 32, .vocab_size = 10,
        .max_seq_len = 8, .rope_theta = 10000.0f
    };
    qmodel_t qm;
    memset(&qm, 0, sizeof(qm));
    qm.config = config;
    /* Create minimal F32 "weights" */
    size_t embed_bytes = 10 * 16 * sizeof(float);
    float *embed_data = (float *)calloc(1, embed_bytes);
    for (int i = 0; i < 10 * 16; i++) embed_data[i] = 0.01f * (i % 7 - 3);
    qm.token_embed = (qtensor_t){ .data = embed_data, .type = 0, .n_elements = 10*16, .n_bytes = embed_bytes };
    float *norm_data = (float *)calloc(16, sizeof(float));
    for (int i = 0; i < 16; i++) norm_data[i] = 1.0f;
    qm.output_norm = (qtensor_t){ .data = norm_data, .type = 0, .n_elements = 16, .n_bytes = 16*sizeof(float) };
    qm.output_weight = qm.token_embed;
    qm.rope_freqs = NULL;
    qlayer_t layer;
    memset(&layer, 0, sizeof(layer));
    float *wq_data = (float *)calloc(16*16, sizeof(float));
    for (int i = 0; i < 16*16; i++) wq_data[i] = (i/16 == i%16) ? 0.1f : 0.0f;
    qtensor_t wq = { .data = wq_data, .type = 0, .n_elements = 16*16, .n_bytes = 16*16*sizeof(float) };
    float *an_data = (float *)calloc(16, sizeof(float));
    for (int i = 0; i < 16; i++) an_data[i] = 1.0f;
    layer.attn_norm = (qtensor_t){ .data = an_data, .type = 0, .n_elements = 16, .n_bytes = 16*sizeof(float) };
    layer.wq = wq; layer.wk = wq; layer.wv = wq; layer.wo = wq;
    layer.ffn_norm = layer.attn_norm;
    float *wg_data = (float *)calloc(32*16, sizeof(float));
    for (int i = 0; i < 32*16; i++) wg_data[i] = 0.01f;
    layer.w_gate = (qtensor_t){ .data = wg_data, .type = 0, .n_elements = 32*16, .n_bytes = 32*16*sizeof(float) };
    layer.w_up = layer.w_gate;
    float *wd_data = (float *)calloc(16*32, sizeof(float));
    for (int i = 0; i < 16*32; i++) wd_data[i] = 0.01f;
    layer.w_down = (qtensor_t){ .data = wd_data, .type = 0, .n_elements = 16*32, .n_bytes = 16*32*sizeof(float) };
    qm.layers = &layer;
    qm.loaded = true;

    qmodel_tq_state_t state;
    if (qmodel_tq_alloc_state(&state, &config, 3, 3, 42) != 0) { printf(" FAIL (alloc)\n"); return 1; }
    qmodel_tq_forward(&qm, &state, 1);
    float sum = 0;
    for (int i = 0; i < 10; i++) sum += fabsf(state.logits[i]);
    if (sum < 1e-10f) { printf(" FAIL (zero logits)\n"); qmodel_tq_free_state(&state); return 1; }
    qmodel_tq_forward(&qm, &state, 2);
    if (state.pos != 2) { printf(" FAIL (pos=%d)\n", state.pos); qmodel_tq_free_state(&state); return 1; }
    qmodel_tq_free_state(&state);
    free(embed_data); free(norm_data); free(wq_data); free(an_data); free(wg_data); free(wd_data);
    printf(" PASS\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== test_qmodel_tq ===\n\n");

    int fails = 0;

    test_alloc_free();
    printf("\n");
    test_memory_savings();
    printf("\n");
    fails += test_forward_no_crash();

    printf("\n=== Results: %d PASS, %d FAIL ===\n", g_pass + (fails == 0 ? 1 : 0), g_fail + fails);
    return (g_fail == 0 && fails == 0) ? 0 : 1;
}
