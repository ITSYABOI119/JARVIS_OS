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
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== test_qmodel_tq ===\n\n");

    test_alloc_free();
    printf("\n");
    test_memory_savings();

    printf("\n=== Results: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
