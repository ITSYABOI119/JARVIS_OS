/*
 * JARVIS AI-OS Phase 3 -- Gemma 4 Config Tests
 *
 * Tests architecture-aware metadata loading: llama_load_config() reads
 * general.architecture from GGUF and dispatches {arch}.* metadata keys.
 *
 * Uses in-memory GGUF builder helpers to construct valid GGUF binaries
 * for testing without real model files.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
 *     -I phase3/src/ai \
 *     phase3/src/ai/test_gemma4_config.c \
 *     phase3/src/ai/llama_load.c \
 *     phase3/src/ai/gguf_parser.c \
 *     phase3/src/ai/dequant.c \
 *     -lm -o /tmp/test_gemma4_config
 *   /tmp/test_gemma4_config
 */

#define _POSIX_C_SOURCE 200809L

#include "llama_model.h"
#include "gguf_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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
    printf("  %-50s ", #fn); \
    fn(); \
    if (fail_count == prev_fail) { \
        printf("PASS\n"); \
        pass_count++; \
    } \
} while (0)

/* ---- GGUF In-Memory Builder ---- */

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} gguf_builder_t;

static void builder_init(gguf_builder_t *b)
{
    b->cap = 8192;
    b->buf = (uint8_t *)malloc(b->cap);
    b->len = 0;
}

static void builder_free(gguf_builder_t *b)
{
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void builder_grow(gguf_builder_t *b, size_t need)
{
    while (b->len + need > b->cap) {
        b->cap *= 2;
        b->buf = (uint8_t *)realloc(b->buf, b->cap);
    }
}

static void builder_write(gguf_builder_t *b, const void *data, size_t n)
{
    builder_grow(b, n);
    memcpy(b->buf + b->len, data, n);
    b->len += n;
}

static void builder_u32(gguf_builder_t *b, uint32_t v)
{
    builder_write(b, &v, 4);
}

static void builder_u64(gguf_builder_t *b, uint64_t v)
{
    builder_write(b, &v, 8);
}

static void builder_f32(gguf_builder_t *b, float v)
{
    builder_write(b, &v, 4);
}

static void builder_gguf_string(gguf_builder_t *b, const char *s)
{
    uint64_t len = (uint64_t)strlen(s);
    builder_u64(b, len);
    builder_write(b, s, (size_t)len);
}

/* Write a KV pair: string key */
static void builder_kv_string(gguf_builder_t *b, const char *key, const char *val)
{
    builder_gguf_string(b, key);
    builder_u32(b, GGUF_TYPE_STRING);
    builder_gguf_string(b, val);
}

/* Write a KV pair: uint32 */
static void builder_kv_u32(gguf_builder_t *b, const char *key, uint32_t val)
{
    builder_gguf_string(b, key);
    builder_u32(b, GGUF_TYPE_UINT32);
    builder_u32(b, val);
}

/* Write a KV pair: float32 */
static void builder_kv_f32(gguf_builder_t *b, const char *key, float val)
{
    builder_gguf_string(b, key);
    builder_u32(b, GGUF_TYPE_FLOAT32);
    builder_f32(b, val);
}

/* Write GGUF header: magic + version + n_tensors + n_kv */
static void builder_header(gguf_builder_t *b, uint64_t n_tensors, uint64_t n_kv)
{
    builder_u32(b, GGUF_MAGIC);     /* 0x46554747 */
    builder_u32(b, 3);              /* version 3 */
    builder_u64(b, n_tensors);
    builder_u64(b, n_kv);
}

/* Write a tensor info entry (for vocab_size fallback testing) */
static void builder_tensor_info(gguf_builder_t *b, const char *name,
                                uint64_t dim0, uint64_t dim1)
{
    builder_gguf_string(b, name);
    builder_u32(b, 2);             /* n_dims = 2 */
    builder_u64(b, dim0);          /* dims[0] */
    builder_u64(b, dim1);          /* dims[1] */
    builder_u32(b, GGML_TYPE_F32); /* type = F32 */
    builder_u64(b, 0);             /* offset = 0 */
}

/* ---- Test 1: Gemma 4 architecture dispatch ---- */
TEST(test_arch_dispatch_gemma4)
{
    gguf_builder_t b;
    builder_init(&b);

    /*
     * Gemma 4 E2B config (simplified):
     *   dim=1536, n_layers=35, n_heads=8, n_kv_heads=1,
     *   head_dim=512 (explicit, NOT 1536/8=192),
     *   hidden_dim=per-layer array (scalar lookup will fail),
     *   vocab_size via tensor shape, rope_theta=1000000,
     *   rms_norm_eps=1e-6, logit_softcap=30.0, ple_dim=256,
     *   swa_window=512, rope_theta_swa=10000,
     *   head_dim_swa=256, shared_kv_layers=20
     */

    /* n_kv = 14 metadata keys + 1 tensor for vocab fallback */
    /* Note: feed_forward_length is an array for Gemma 4 -- we omit the scalar
     * version so gguf_get_kv_u32 fails and hidden_dim stays 0.
     * Per-layer array loading is Task 2. */
    uint64_t n_kv = 14;
    uint64_t n_tensors = 1;

    builder_header(&b, n_tensors, n_kv);

    /* KV pairs */
    builder_kv_string(&b, "general.architecture", "gemma4");
    builder_kv_u32(&b, "gemma4.embedding_length", 1536);
    builder_kv_u32(&b, "gemma4.block_count", 35);
    builder_kv_u32(&b, "gemma4.attention.head_count", 8);
    builder_kv_u32(&b, "gemma4.attention.head_count_kv", 1);
    builder_kv_u32(&b, "gemma4.attention.key_length", 512);
    builder_kv_f32(&b, "gemma4.rope.freq_base", 1000000.0f);
    builder_kv_f32(&b, "gemma4.attention.layer_norm_rms_epsilon", 1e-6f);
    builder_kv_f32(&b, "gemma4.final_logit_softcapping", 30.0f);
    builder_kv_u32(&b, "gemma4.embedding_length_per_layer_input", 256);
    builder_kv_u32(&b, "gemma4.attention.sliding_window", 512);
    builder_kv_f32(&b, "gemma4.rope.freq_base_swa", 10000.0f);
    builder_kv_u32(&b, "gemma4.attention.key_length_swa", 256);
    builder_kv_u32(&b, "gemma4.attention.shared_kv_layers", 20);

    /* feed_forward_length is array => gguf_get_kv_u32 will fail, hidden_dim stays 0 */
    /* (We do NOT add it as scalar, to test the gemma4 fallback path) */

    /* Tensor info: token_embd.weight for vocab_size fallback */
    builder_tensor_info(&b, "token_embd.weight", 1536, 262144);

    /* Parse */
    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    /* Load config */
    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed for gemma4");

    /* Verify core fields */
    ASSERT(config.dim == 1536, "dim should be 1536");
    ASSERT(config.n_layers == 35, "n_layers should be 35");
    ASSERT(config.n_heads == 8, "n_heads should be 8");
    ASSERT(config.n_kv_heads == 1, "n_kv_heads should be 1");
    ASSERT(config.head_dim == 512, "head_dim should be 512 (explicit, not 1536/8)");
    ASSERT(config.hidden_dim == 0, "hidden_dim should be 0 (array, Task 2)");
    ASSERT(config.vocab_size == 262144, "vocab_size should be 262144 (from tensor)");
    ASSERT(fabsf(config.rope_theta - 1000000.0f) < 1.0f, "rope_theta should be 1000000");

    /* Verify Gemma 4 extension fields */
    ASSERT(fabsf(config.rms_norm_eps - 1e-6f) < 1e-9f, "rms_norm_eps should be 1e-6");
    ASSERT(fabsf(config.logit_softcap - 30.0f) < 0.01f, "logit_softcap should be 30.0");
    ASSERT(config.ple_dim == 256, "ple_dim should be 256");
    ASSERT(config.swa_window == 512, "swa_window should be 512");
    ASSERT(fabsf(config.rope_theta_swa - 10000.0f) < 1.0f, "rope_theta_swa should be 10000");
    ASSERT(config.head_dim_swa == 256, "head_dim_swa should be 256");
    ASSERT(config.shared_kv_layers == 20, "shared_kv_layers should be 20");

    /* Per-layer arrays should be NULL (not loaded in this task) */
    ASSERT(config.layer_ffn_dim == NULL, "layer_ffn_dim should be NULL");
    ASSERT(config.layer_is_swa == NULL, "layer_is_swa should be NULL");
    ASSERT(config.kv_share_map == NULL, "kv_share_map should be NULL");

    llama_free_config(&config);
    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Test 2: Llama architecture dispatch ---- */
TEST(test_arch_dispatch_llama)
{
    gguf_builder_t b;
    builder_init(&b);

    /*
     * Llama 3.2 1B-like config:
     *   dim=2048, n_layers=16, n_heads=32, n_kv_heads=8,
     *   head_dim=64 (derived from 2048/32),
     *   hidden_dim=8192, vocab_size=128256,
     *   rope_theta=500000
     */

    uint64_t n_kv = 8;
    uint64_t n_tensors = 0;

    builder_header(&b, n_tensors, n_kv);

    builder_kv_string(&b, "general.architecture", "llama");
    builder_kv_u32(&b, "llama.embedding_length", 2048);
    builder_kv_u32(&b, "llama.block_count", 16);
    builder_kv_u32(&b, "llama.attention.head_count", 32);
    builder_kv_u32(&b, "llama.attention.head_count_kv", 8);
    builder_kv_u32(&b, "llama.feed_forward_length", 8192);
    builder_kv_u32(&b, "llama.vocab_size", 128256);
    builder_kv_f32(&b, "llama.rope.freq_base", 500000.0f);

    /* Parse */
    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    /* Load config */
    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed for llama");

    /* Verify core fields */
    ASSERT(config.dim == 2048, "dim should be 2048");
    ASSERT(config.n_layers == 16, "n_layers should be 16");
    ASSERT(config.n_heads == 32, "n_heads should be 32");
    ASSERT(config.n_kv_heads == 8, "n_kv_heads should be 8");
    ASSERT(config.head_dim == 64, "head_dim should be 64 (derived from 2048/32)");
    ASSERT(config.hidden_dim == 8192, "hidden_dim should be 8192");
    ASSERT(config.vocab_size == 128256, "vocab_size should be 128256");
    ASSERT(fabsf(config.rope_theta - 500000.0f) < 1.0f, "rope_theta should be 500000");

    /* All Gemma 4 extension fields should be 0/NULL */
    ASSERT(config.rms_norm_eps == 0.0f, "rms_norm_eps should be 0 for llama");
    ASSERT(config.logit_softcap == 0.0f, "logit_softcap should be 0 for llama");
    ASSERT(config.rope_theta_swa == 0.0f, "rope_theta_swa should be 0 for llama");
    ASSERT(config.ple_dim == 0, "ple_dim should be 0 for llama");
    ASSERT(config.swa_window == 0, "swa_window should be 0 for llama");
    ASSERT(config.shared_kv_layers == 0, "shared_kv_layers should be 0 for llama");
    ASSERT(config.head_dim_swa == 0, "head_dim_swa should be 0 for llama");
    ASSERT(config.layer_ffn_dim == NULL, "layer_ffn_dim should be NULL for llama");
    ASSERT(config.layer_is_swa == NULL, "layer_is_swa should be NULL for llama");
    ASSERT(config.kv_share_map == NULL, "kv_share_map should be NULL for llama");

    llama_free_config(&config);
    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Test 3: No arch key defaults to llama ---- */
TEST(test_no_arch_key_defaults_to_llama)
{
    gguf_builder_t b;
    builder_init(&b);

    /*
     * Same as llama config, but WITHOUT "general.architecture".
     * Should default to "llama" prefix and still work.
     */

    uint64_t n_kv = 7;  /* no general.architecture key */
    uint64_t n_tensors = 0;

    builder_header(&b, n_tensors, n_kv);

    builder_kv_u32(&b, "llama.embedding_length", 2048);
    builder_kv_u32(&b, "llama.block_count", 16);
    builder_kv_u32(&b, "llama.attention.head_count", 32);
    builder_kv_u32(&b, "llama.attention.head_count_kv", 8);
    builder_kv_u32(&b, "llama.feed_forward_length", 8192);
    builder_kv_u32(&b, "llama.vocab_size", 128256);
    builder_kv_f32(&b, "llama.rope.freq_base", 500000.0f);

    /* Parse */
    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    /* Load config */
    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed without general.architecture");

    /* Verify same results as explicit llama */
    ASSERT(config.dim == 2048, "dim should be 2048");
    ASSERT(config.n_layers == 16, "n_layers should be 16");
    ASSERT(config.n_heads == 32, "n_heads should be 32");
    ASSERT(config.n_kv_heads == 8, "n_kv_heads should be 8");
    ASSERT(config.head_dim == 64, "head_dim should be 64");
    ASSERT(config.hidden_dim == 8192, "hidden_dim should be 8192");
    ASSERT(config.vocab_size == 128256, "vocab_size should be 128256");
    ASSERT(fabsf(config.rope_theta - 500000.0f) < 1.0f, "rope_theta should be 500000");

    /* Extension fields should be 0 */
    ASSERT(config.rms_norm_eps == 0.0f, "rms_norm_eps should be 0");
    ASSERT(config.ple_dim == 0, "ple_dim should be 0");
    ASSERT(config.swa_window == 0, "swa_window should be 0");

    llama_free_config(&config);
    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Test 4: gguf_find_kv() helper ---- */
TEST(test_gguf_find_kv)
{
    gguf_builder_t b;
    builder_init(&b);

    uint64_t n_kv = 3;
    uint64_t n_tensors = 0;

    builder_header(&b, n_tensors, n_kv);

    builder_kv_string(&b, "general.architecture", "test");
    builder_kv_u32(&b, "test.embedding_length", 42);
    builder_kv_f32(&b, "test.rope.freq_base", 3.14f);

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    /* Find existing keys */
    const gguf_kv_t *kv = gguf_find_kv(&ctx, "general.architecture");
    ASSERT(kv != NULL, "should find general.architecture");
    ASSERT(kv->type == GGUF_TYPE_STRING, "type should be STRING");
    ASSERT(strcmp(kv->value.s.str, "test") == 0, "value should be 'test'");

    kv = gguf_find_kv(&ctx, "test.embedding_length");
    ASSERT(kv != NULL, "should find test.embedding_length");
    ASSERT(kv->type == GGUF_TYPE_UINT32, "type should be UINT32");
    ASSERT(kv->value.u32 == 42, "value should be 42");

    kv = gguf_find_kv(&ctx, "test.rope.freq_base");
    ASSERT(kv != NULL, "should find test.rope.freq_base");
    ASSERT(kv->type == GGUF_TYPE_FLOAT32, "type should be FLOAT32");
    ASSERT(fabsf(kv->value.f32 - 3.14f) < 0.001f, "value should be ~3.14");

    /* Non-existent key */
    kv = gguf_find_kv(&ctx, "nonexistent.key");
    ASSERT(kv == NULL, "should return NULL for nonexistent key");

    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Test 5: llama_free_config() safety ---- */
TEST(test_free_config_safety)
{
    /* Test that llama_free_config() handles NULL fields and double-free */
    llama_config_t config;
    memset(&config, 0, sizeof(config));

    /* Free with all NULLs (should be safe) */
    llama_free_config(&config);
    ASSERT(config.layer_ffn_dim == NULL, "should remain NULL");
    ASSERT(config.layer_is_swa == NULL, "should remain NULL");
    ASSERT(config.kv_share_map == NULL, "should remain NULL");

    /* Allocate some arrays, then free */
    config.layer_ffn_dim = (int *)malloc(10 * sizeof(int));
    config.layer_is_swa = (bool *)malloc(10 * sizeof(bool));
    config.kv_share_map = (int *)malloc(10 * sizeof(int));

    llama_free_config(&config);
    ASSERT(config.layer_ffn_dim == NULL, "should be NULL after free");
    ASSERT(config.layer_is_swa == NULL, "should be NULL after free");
    ASSERT(config.kv_share_map == NULL, "should be NULL after free");

    /* Double free (should be safe since pointers are NULLed) */
    llama_free_config(&config);

    /* NULL config (should not crash) */
    llama_free_config(NULL);
}

/* ---- Test 6: Explicit head_dim vs derived ---- */
TEST(test_explicit_vs_derived_head_dim)
{
    gguf_builder_t b;
    builder_init(&b);

    /* Config where key_length is explicit and != dim/n_heads */
    uint64_t n_kv = 8;
    builder_header(&b, 0, n_kv);

    builder_kv_string(&b, "general.architecture", "llama");
    builder_kv_u32(&b, "llama.embedding_length", 2048);
    builder_kv_u32(&b, "llama.block_count", 16);
    builder_kv_u32(&b, "llama.attention.head_count", 32);
    builder_kv_u32(&b, "llama.attention.head_count_kv", 8);
    builder_kv_u32(&b, "llama.attention.key_length", 128);  /* Explicit, != 2048/32=64 */
    builder_kv_u32(&b, "llama.feed_forward_length", 8192);
    builder_kv_u32(&b, "llama.vocab_size", 128256);

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed");
    ASSERT(config.head_dim == 128, "head_dim should be 128 (explicit key_length)");

    llama_free_config(&config);
    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Test 7: Max seq len capping ---- */
TEST(test_max_seq_len_capping)
{
    gguf_builder_t b;
    builder_init(&b);

    uint64_t n_kv = 8;
    builder_header(&b, 0, n_kv);

    builder_kv_string(&b, "general.architecture", "llama");
    builder_kv_u32(&b, "llama.embedding_length", 2048);
    builder_kv_u32(&b, "llama.block_count", 16);
    builder_kv_u32(&b, "llama.attention.head_count", 32);
    builder_kv_u32(&b, "llama.attention.head_count_kv", 8);
    builder_kv_u32(&b, "llama.feed_forward_length", 8192);
    builder_kv_u32(&b, "llama.vocab_size", 128256);
    builder_kv_u32(&b, "llama.context_length", 131072);  /* Huge, should be capped */

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, b.buf, b.len);
    ASSERT(err == GGUF_OK, "gguf_open_memory should succeed");

    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config should succeed");
    ASSERT(config.max_seq_len == LLAMA_MAX_SEQ_LEN,
           "max_seq_len should be capped at LLAMA_MAX_SEQ_LEN");

    llama_free_config(&config);
    gguf_close(&ctx);
    builder_free(&b);
}

/* ---- Main ---- */

int main(void)
{
    int prev_fail;

    printf("=== Gemma 4 Config Tests ===\n\n");

    prev_fail = fail_count; RUN(test_arch_dispatch_gemma4);
    prev_fail = fail_count; RUN(test_arch_dispatch_llama);
    prev_fail = fail_count; RUN(test_no_arch_key_defaults_to_llama);
    prev_fail = fail_count; RUN(test_gguf_find_kv);
    prev_fail = fail_count; RUN(test_free_config_safety);
    prev_fail = fail_count; RUN(test_explicit_vs_derived_head_dim);
    prev_fail = fail_count; RUN(test_max_seq_len_capping);

    printf("\n--- Results: %d PASS, %d FAIL ---\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
