/*
 * JARVIS AI-OS Phase 3 — Llama Model Load Tests
 *
 * Unit tests for llama_model.h / llama_load.c: config, allocation,
 * weight buffer access, KV cache, and alloc/free cycles.
 *
 * Tiny config: dim=8, n_layers=2, n_heads=2, n_kv_heads=2,
 *   head_dim=4, hidden_dim=16, vocab_size=32, max_seq_len=16
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *     phase3/src/ai/test_llama_load.c phase3/src/ai/llama_load.c \
 *     phase3/src/ai/dequant.c phase3/src/ai/gguf_parser.c \
 *     -lm -o /tmp/test_llama_load && /tmp/test_llama_load
 */

#include "llama_model.h"
#include <stdio.h>
#include <string.h>

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name) static void name(void)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        fail_count++; \
        return; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("  %-40s ", #fn); \
    fn(); \
    if (fail_count == prev_fail) { \
        printf("PASS\n"); \
        pass_count++; \
    } \
} while (0)

/* Tiny model config for testing */
static void init_tiny_config(llama_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->dim         = 8;
    c->n_layers    = 2;
    c->n_heads     = 2;
    c->n_kv_heads  = 2;
    c->head_dim    = 4;   /* dim / n_heads = 8 / 2 = 4 */
    c->hidden_dim  = 16;
    c->vocab_size  = 32;
    c->max_seq_len = 16;
    c->rope_theta  = 500000.0f;
}

/* ---- Test 1: Allocate model and verify pointers ---- */
TEST(test_alloc_model)
{
    llama_model_t model;
    memset(&model, 0, sizeof(model));
    init_tiny_config(&model.config);

    int err = llama_alloc_model(&model);
    ASSERT(err == 0, "llama_alloc_model should return 0");
    ASSERT(model.token_embed != NULL, "token_embed should be allocated");
    ASSERT(model.output_norm != NULL, "output_norm should be allocated");
    ASSERT(model.output_weight != NULL, "output_weight should be allocated");
    ASSERT(model.layers != NULL, "layers should be allocated");
    ASSERT(model.total_bytes > 0, "total_bytes should be > 0");
    ASSERT(model.loaded == false, "loaded should be false before weight load");

    /* Verify layer pointers */
    for (int i = 0; i < model.config.n_layers; i++) {
        llama_layer_t *L = &model.layers[i];
        ASSERT(L->attn_norm != NULL, "layer attn_norm should be allocated");
        ASSERT(L->wq != NULL, "layer wq should be allocated");
        ASSERT(L->wk != NULL, "layer wk should be allocated");
        ASSERT(L->wv != NULL, "layer wv should be allocated");
        ASSERT(L->wo != NULL, "layer wo should be allocated");
        ASSERT(L->ffn_norm != NULL, "layer ffn_norm should be allocated");
        ASSERT(L->w_gate != NULL, "layer w_gate should be allocated");
        ASSERT(L->w_up != NULL, "layer w_up should be allocated");
        ASSERT(L->w_down != NULL, "layer w_down should be allocated");
    }

    llama_free_model(&model);
}

/* ---- Test 2: Allocate state and verify buffers ---- */
TEST(test_alloc_state)
{
    llama_config_t config;
    init_tiny_config(&config);

    llama_state_t state;
    int err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "llama_alloc_state should return 0");
    ASSERT(state.x != NULL, "x buffer should be allocated");
    ASSERT(state.xb != NULL, "xb buffer should be allocated");
    ASSERT(state.xb2 != NULL, "xb2 buffer should be allocated");
    ASSERT(state.q != NULL, "q buffer should be allocated");
    ASSERT(state.k != NULL, "k buffer should be allocated");
    ASSERT(state.v != NULL, "v buffer should be allocated");
    ASSERT(state.att != NULL, "att buffer should be allocated");
    ASSERT(state.hb != NULL, "hb buffer should be allocated");
    ASSERT(state.hb2 != NULL, "hb2 buffer should be allocated");
    ASSERT(state.logits != NULL, "logits buffer should be allocated");
    ASSERT(state.key_cache != NULL, "key_cache should be allocated");
    ASSERT(state.value_cache != NULL, "value_cache should be allocated");
    ASSERT(state.pos == 0, "pos should be 0");
    ASSERT(state.max_seq_len == 16, "max_seq_len should match config");

    llama_free_state(&state);
}

/* ---- Test 3: head_dim computed correctly ---- */
TEST(test_config_head_dim)
{
    llama_config_t config;
    init_tiny_config(&config);

    /* Verify default: dim=8, n_heads=2 => head_dim=4 */
    ASSERT(config.head_dim == 4, "head_dim should be dim/n_heads = 4");

    /* Try different config */
    config.dim = 64;
    config.n_heads = 8;
    config.head_dim = config.dim / config.n_heads;
    ASSERT(config.head_dim == 8, "head_dim should be 64/8 = 8");

    /* Another config */
    config.dim = 128;
    config.n_heads = 4;
    config.head_dim = config.dim / config.n_heads;
    ASSERT(config.head_dim == 32, "head_dim should be 128/4 = 32");
}

/* ---- Test 4: Alloc/free cycle (no crash, no leak) ---- */
TEST(test_alloc_free_cycle)
{
    llama_model_t model;
    llama_state_t state;
    llama_config_t config;
    init_tiny_config(&config);

    /* First cycle */
    memset(&model, 0, sizeof(model));
    model.config = config;
    int err = llama_alloc_model(&model);
    ASSERT(err == 0, "first alloc_model should succeed");
    llama_free_model(&model);
    ASSERT(model.token_embed == NULL, "token_embed should be NULL after free");
    ASSERT(model.layers == NULL, "layers should be NULL after free");

    /* Second cycle */
    memset(&model, 0, sizeof(model));
    model.config = config;
    err = llama_alloc_model(&model);
    ASSERT(err == 0, "second alloc_model should succeed");
    llama_free_model(&model);

    /* State cycle */
    err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "first alloc_state should succeed");
    llama_free_state(&state);
    ASSERT(state.x == NULL, "x should be NULL after free");

    err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "second alloc_state should succeed");
    llama_free_state(&state);
}

/* ---- Test 5: Layer weight sizes — write/read at expected positions ---- */
TEST(test_layer_weight_sizes)
{
    llama_model_t model;
    memset(&model, 0, sizeof(model));
    init_tiny_config(&model.config);

    int err = llama_alloc_model(&model);
    ASSERT(err == 0, "alloc_model should succeed");

    int dim       = model.config.dim;         /* 8 */
    int n_heads   = model.config.n_heads;     /* 2 */
    int head_dim  = model.config.head_dim;    /* 4 */
    int n_kv      = model.config.n_kv_heads;  /* 2 */
    int hidden    = model.config.hidden_dim;  /* 16 */

    /* wq: n_heads * head_dim * dim = 2*4*8 = 64 floats */
    int wq_n = n_heads * head_dim * dim;
    ASSERT(wq_n == 64, "wq should have 64 elements");
    model.layers[0].wq[0] = 1.0f;
    model.layers[0].wq[wq_n - 1] = 2.0f;
    ASSERT(model.layers[0].wq[0] == 1.0f, "wq[0] should be 1.0");
    ASSERT(model.layers[0].wq[wq_n - 1] == 2.0f, "wq[last] should be 2.0");

    /* wk: n_kv * head_dim * dim = 2*4*8 = 64 floats */
    int wk_n = n_kv * head_dim * dim;
    ASSERT(wk_n == 64, "wk should have 64 elements");
    model.layers[1].wk[0] = 3.0f;
    model.layers[1].wk[wk_n - 1] = 4.0f;
    ASSERT(model.layers[1].wk[0] == 3.0f, "wk[0] should be 3.0");
    ASSERT(model.layers[1].wk[wk_n - 1] == 4.0f, "wk[last] should be 4.0");

    /* w_gate: hidden_dim * dim = 16*8 = 128 floats */
    int gate_n = hidden * dim;
    ASSERT(gate_n == 128, "w_gate should have 128 elements");
    model.layers[0].w_gate[0] = 5.0f;
    model.layers[0].w_gate[gate_n - 1] = 6.0f;
    ASSERT(model.layers[0].w_gate[0] == 5.0f, "w_gate[0] should be 5.0");
    ASSERT(model.layers[0].w_gate[gate_n - 1] == 6.0f, "w_gate[last] should be 6.0");

    llama_free_model(&model);
}

/* ---- Test 6: Token embedding write/read ---- */
TEST(test_embedding_write_read)
{
    llama_model_t model;
    memset(&model, 0, sizeof(model));
    init_tiny_config(&model.config);

    int err = llama_alloc_model(&model);
    ASSERT(err == 0, "alloc_model should succeed");

    int vocab_size = model.config.vocab_size;  /* 32 */
    int dim = model.config.dim;                /* 8 */
    int total = vocab_size * dim;              /* 256 */

    /* Write known pattern */
    for (int i = 0; i < total; i++)
        model.token_embed[i] = (float)i * 0.1f;

    /* Read back and verify */
    ASSERT(model.token_embed[0] == 0.0f, "embed[0] should be 0.0");
    ASSERT(model.token_embed[1] == 0.1f, "embed[1] should be 0.1");
    ASSERT(model.token_embed[total - 1] == (float)(total - 1) * 0.1f,
           "embed[last] should match pattern");

    /* Also test output_norm */
    for (int i = 0; i < dim; i++)
        model.output_norm[i] = (float)(i + 1);
    ASSERT(model.output_norm[0] == 1.0f, "output_norm[0] should be 1.0");
    ASSERT(model.output_norm[dim - 1] == (float)dim, "output_norm[last] should match");

    llama_free_model(&model);
}

/* ---- Test 7: KV cache write at last valid index ---- */
TEST(test_kv_cache_write)
{
    llama_config_t config;
    init_tiny_config(&config);

    llama_state_t state;
    int err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "alloc_state should succeed");

    /* KV cache size: n_layers * max_seq_len * n_kv_heads * head_dim
     * = 2 * 16 * 2 * 4 = 256 floats per cache */
    size_t kv_total = (size_t)config.n_layers * (size_t)config.max_seq_len *
                      (size_t)config.n_kv_heads * (size_t)config.head_dim;
    ASSERT(kv_total == 256, "kv_total should be 256");

    /* Write at first and last valid index */
    state.key_cache[0] = 42.0f;
    state.key_cache[kv_total - 1] = 99.0f;
    ASSERT(state.key_cache[0] == 42.0f, "key_cache[0] should be 42.0");
    ASSERT(state.key_cache[kv_total - 1] == 99.0f, "key_cache[last] should be 99.0");

    state.value_cache[0] = -1.0f;
    state.value_cache[kv_total - 1] = -2.0f;
    ASSERT(state.value_cache[0] == -1.0f, "value_cache[0] should be -1.0");
    ASSERT(state.value_cache[kv_total - 1] == -2.0f, "value_cache[last] should be -2.0");

    llama_free_state(&state);
}

/* ---- Test 8: shared-KV (Gemma 4) sizes the KV cache by n_unique ---- */
TEST(test_kv_cache_shared)
{
    /* Gemma 4 E2B KV shape: 35 layers, 20 shared -> 15 unique own-KV slots. */
    llama_config_t config;
    memset(&config, 0, sizeof(config));
    config.dim              = 512;
    config.n_layers         = 35;
    config.n_heads          = 1;
    config.n_kv_heads       = 1;
    config.head_dim         = 512;   /* head_dim_swa == 0 -> max_head_dim == 512 */
    config.hidden_dim       = 16;
    config.vocab_size       = 32;
    config.max_seq_len      = 512;
    config.shared_kv_layers = 20;    /* -> kv_n_layers = 35 - 20 = 15 */

    llama_state_t state;
    int err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "shared-KV alloc_state should succeed");
    ASSERT(state.kv_n_layers == 15, "kv_n_layers should be 15 (35 - 20 shared)");

    /* The cache holds exactly kv_n_layers * max_seq * (n_kv_heads*max_head_dim) floats;
     * the last own-KV slot's final element must be writable (no OOB). */
    size_t kv_total = (size_t)state.kv_n_layers * (size_t)config.max_seq_len *
                      (size_t)config.n_kv_heads * (size_t)config.head_dim;
    ASSERT(kv_total == (size_t)15 * 512 * 512, "kv_total should be 15*512*512");
    state.key_cache[0]              = 7.0f;
    state.key_cache[kv_total - 1]   = 11.0f;
    state.value_cache[kv_total - 1] = 13.0f;
    ASSERT(state.key_cache[0] == 7.0f, "key_cache[0] writable");
    ASSERT(state.key_cache[kv_total - 1] == 11.0f, "key_cache[last unique slot] writable");
    ASSERT(state.value_cache[kv_total - 1] == 13.0f, "value_cache[last unique slot] writable");

    llama_free_state(&state);
}

/* ---- Test 9: non-shared model allocates full n_layers KV slots (unaffected) ---- */
TEST(test_kv_cache_nonshared)
{
    llama_config_t config;
    init_tiny_config(&config);   /* shared_kv_layers == 0 (zero-inited) */
    ASSERT(config.shared_kv_layers == 0, "tiny config is non-shared");

    llama_state_t state;
    int err = llama_alloc_state(&state, &config);
    ASSERT(err == 0, "non-shared alloc_state should succeed");
    ASSERT(state.kv_n_layers == config.n_layers, "non-shared kv_n_layers == n_layers (unaffected)");

    llama_free_state(&state);
}

/* ---- Synthetic gemma4 GGUF fixture (KV metadata only, no tensors) ---- */

static size_t fx_put_string(uint8_t *p, const char *s)
{
    uint64_t len = strlen(s);
    memcpy(p, &len, 8);
    memcpy(p + 8, s, len);
    return 8 + len;
}

static size_t fx_put_kv_str(uint8_t *p, const char *key, const char *val)
{
    uint8_t *start = p;
    p += fx_put_string(p, key);
    uint32_t t = GGUF_TYPE_STRING;
    memcpy(p, &t, 4); p += 4;
    p += fx_put_string(p, val);
    return (size_t)(p - start);
}

static size_t fx_put_kv_u32(uint8_t *p, const char *key, uint32_t val)
{
    uint8_t *start = p;
    p += fx_put_string(p, key);
    uint32_t t = GGUF_TYPE_UINT32;
    memcpy(p, &t, 4); p += 4;
    memcpy(p, &val, 4); p += 4;
    return (size_t)(p - start);
}

static size_t fx_put_kv_arr_u32(uint8_t *p, const char *key,
                                const uint32_t *vals, uint64_t count)
{
    uint8_t *start = p;
    p += fx_put_string(p, key);
    uint32_t t = GGUF_TYPE_ARRAY, et = GGUF_TYPE_UINT32;
    memcpy(p, &t, 4); p += 4;
    memcpy(p, &et, 4); p += 4;
    memcpy(p, &count, 8); p += 8;
    memcpy(p, vals, (size_t)count * 4); p += count * 4;
    return (size_t)(p - start);
}

static size_t fx_put_kv_arr_bool(uint8_t *p, const char *key,
                                 const uint8_t *vals, uint64_t count)
{
    uint8_t *start = p;
    p += fx_put_string(p, key);
    uint32_t t = GGUF_TYPE_ARRAY, et = GGUF_TYPE_BOOL;
    memcpy(p, &t, 4); p += 4;
    memcpy(p, &et, 4); p += 4;
    memcpy(p, &count, 8); p += 8;
    memcpy(p, vals, (size_t)count); p += count;
    return (size_t)(p - start);
}

/* variant 0: per-layer pattern + FFN arrays present
 * variant 1: neither present (heuristic fallback)
 * variant 2: scalar sliding_window_pattern (period), no FFN array */
static size_t build_gemma4_gguf(uint8_t *buf, size_t buf_size, int variant)
{
    memset(buf, 0, buf_size);
    uint8_t *p = buf;

    uint32_t magic = GGUF_MAGIC, version = 3;
    uint64_t n_tensors = 0;
    uint64_t n_kv = (variant == 0) ? 9 : (variant == 2) ? 8 : 7;
    memcpy(p, &magic, 4);     p += 4;
    memcpy(p, &version, 4);   p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8);      p += 8;

    p += fx_put_kv_str(p, "general.architecture", "gemma4");
    p += fx_put_kv_u32(p, "gemma4.embedding_length", 8);
    p += fx_put_kv_u32(p, "gemma4.block_count", 10);
    p += fx_put_kv_u32(p, "gemma4.attention.head_count", 2);
    p += fx_put_kv_u32(p, "gemma4.vocab_size", 32);
    p += fx_put_kv_u32(p, "gemma4.attention.sliding_window", 4);
    p += fx_put_kv_u32(p, "gemma4.attention.shared_kv_layers", 4);

    if (variant == 0) {
        /* Period-3 pattern (differs from the period-5 default) — 3 divides
         * n_unique=6 so the modular KV-share map stays type-consistent */
        uint8_t pattern[10] = {1, 1, 0, 1, 1, 0, 1, 1, 0, 1};
        p += fx_put_kv_arr_bool(p, "gemma4.attention.sliding_window_pattern",
                                pattern, 10);
        uint32_t ffn[10];
        for (int i = 0; i < 10; i++) ffn[i] = 100u * (uint32_t)(i + 1);
        p += fx_put_kv_arr_u32(p, "gemma4.feed_forward_length", ffn, 10);
    } else if (variant == 2) {
        p += fx_put_kv_u32(p, "gemma4.attention.sliding_window_pattern", 2);
    }

    return (size_t)(p - buf);
}

/* ---- Test 10: config reads per-layer GGUF arrays (pattern + FFN dims) ---- */
TEST(test_config_gguf_arrays)
{
    uint8_t buf[4096];
    size_t sz = build_gemma4_gguf(buf, sizeof(buf), 0);

    gguf_ctx_t ctx;
    ASSERT(gguf_open_memory(&ctx, buf, sz) == GGUF_OK, "gguf_open_memory failed");

    llama_config_t config;
    int err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed");

    /* SWA pattern from the array: period-3, NOT the period-5 default */
    ASSERT(config.layer_is_swa != NULL, "layer_is_swa should be allocated");
    for (int i = 0; i < 10; i++) {
        bool expect = ((i % 3) != 2);
        ASSERT(config.layer_is_swa[i] == expect, "SWA pattern should match GGUF array");
    }

    /* FFN dims from the array; hidden_dim = per-layer max */
    ASSERT(config.layer_ffn_dim != NULL, "layer_ffn_dim should be allocated");
    for (int i = 0; i < 10; i++)
        ASSERT(config.layer_ffn_dim[i] == 100 * (i + 1), "FFN dim should match GGUF array");
    ASSERT(config.hidden_dim == 1000, "hidden_dim should be per-layer max (1000)");

    llama_free_config(&config);
    gguf_close(&ctx);
}

/* ---- Test 11: config falls back to heuristics when arrays absent ---- */
TEST(test_config_heuristic_fallback)
{
    uint8_t buf[4096];
    size_t sz = build_gemma4_gguf(buf, sizeof(buf), 1);

    gguf_ctx_t ctx;
    ASSERT(gguf_open_memory(&ctx, buf, sz) == GGUF_OK, "gguf_open_memory failed");

    llama_config_t config;
    int err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed");

    /* Default period-5 pattern: every 5th layer (i % 5 == 4) is global */
    ASSERT(config.layer_is_swa != NULL, "layer_is_swa should be allocated");
    for (int i = 0; i < 10; i++)
        ASSERT(config.layer_is_swa[i] == ((i % 5) != 4), "default period-5 pattern expected");

    /* Heuristic FFN: n_unique=6 -> layers 0-5 get 6144, 6-9 get 12288 */
    ASSERT(config.layer_ffn_dim != NULL, "layer_ffn_dim should be allocated");
    for (int i = 0; i < 10; i++)
        ASSERT(config.layer_ffn_dim[i] == (i < 6 ? 6144 : 12288), "heuristic FFN dims expected");
    ASSERT(config.hidden_dim == 12288, "heuristic hidden_dim should be 12288");

    llama_free_config(&config);
    gguf_close(&ctx);
}

/* ---- Test 12: scalar sliding_window_pattern acts as period ---- */
TEST(test_config_scalar_pattern)
{
    uint8_t buf[4096];
    size_t sz = build_gemma4_gguf(buf, sizeof(buf), 2);

    gguf_ctx_t ctx;
    ASSERT(gguf_open_memory(&ctx, buf, sz) == GGUF_OK, "gguf_open_memory failed");

    llama_config_t config;
    int err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed");

    /* Scalar period 2: every 2nd layer (i % 2 == 1) is global */
    ASSERT(config.layer_is_swa != NULL, "layer_is_swa should be allocated");
    for (int i = 0; i < 10; i++)
        ASSERT(config.layer_is_swa[i] == ((i % 2) != 1), "period-2 pattern expected");

    llama_free_config(&config);
    gguf_close(&ctx);
}

/* ---- Main ---- */

int main(void)
{
    int prev_fail;

    printf("=== Llama Model Load Tests ===\n\n");

    prev_fail = fail_count; RUN(test_alloc_model);
    prev_fail = fail_count; RUN(test_alloc_state);
    prev_fail = fail_count; RUN(test_config_head_dim);
    prev_fail = fail_count; RUN(test_alloc_free_cycle);
    prev_fail = fail_count; RUN(test_layer_weight_sizes);
    prev_fail = fail_count; RUN(test_embedding_write_read);
    prev_fail = fail_count; RUN(test_kv_cache_write);
    prev_fail = fail_count; RUN(test_kv_cache_shared);
    prev_fail = fail_count; RUN(test_kv_cache_nonshared);
    prev_fail = fail_count; RUN(test_config_gguf_arrays);
    prev_fail = fail_count; RUN(test_config_heuristic_fallback);
    prev_fail = fail_count; RUN(test_config_scalar_pattern);

    printf("\n--- Results: %d PASS, %d FAIL ---\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
