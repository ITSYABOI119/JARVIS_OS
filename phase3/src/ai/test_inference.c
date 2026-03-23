/*
 * JARVIS AI-OS — Inference API Tests
 * Tests the top-level inference_query() wrapper with a tiny synthetic model
 * and the test vocabulary from test_tokenizer.c.
 */

#include "inference.h"
#include "sampling.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int pass = 0, fail = 0, total = 0;
#define TEST(name) do { total++; printf("Test %d: %s ... ", total, name); } while(0)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL_MSG(m) do { fail++; printf("FAIL: %s\n", m); } while(0)
#define CHECK(c, m) do { if (c) PASS(); else FAIL_MSG(m); } while(0)

/* ---- Tiny model setup (same as test_llama_forward.c) ---- */

static void fill_weights(float *data, int n, uint64_t *seed)
{
    for (int i = 0; i < n; i++)
        data[i] = (sampling_rng_float(seed) - 0.5f) * 0.1f;
}

static void fill_norms(llama_model_t *m)
{
    int dim = m->config.dim;
    for (int i = 0; i < dim; i++) m->output_norm[i] = 1.0f;
    for (int L = 0; L < m->config.n_layers; L++) {
        for (int i = 0; i < dim; i++) {
            m->layers[L].attn_norm[i] = 1.0f;
            m->layers[L].ffn_norm[i] = 1.0f;
        }
    }
}

static int init_tiny_ctx(inference_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->model.config = (llama_config_t){
        .dim = 4, .n_layers = 1, .n_heads = 2, .n_kv_heads = 1,
        .head_dim = 2, .hidden_dim = 8, .vocab_size = 17,
        .max_seq_len = 8, .rope_theta = 10000.0f,
    };

    if (llama_alloc_model(&ctx->model) != 0) return -1;

    uint64_t seed = 42;
    int dim = 4, hd = 8, vs = 17, kvd = 2;
    fill_weights(ctx->model.token_embed, vs * dim, &seed);
    fill_weights(ctx->model.output_weight, vs * dim, &seed);
    for (int L = 0; L < 1; L++) {
        llama_layer_t *l = &ctx->model.layers[L];
        fill_weights(l->wq, dim * dim, &seed);
        fill_weights(l->wk, kvd * dim, &seed);
        fill_weights(l->wv, kvd * dim, &seed);
        fill_weights(l->wo, dim * dim, &seed);
        fill_weights(l->w_gate, hd * dim, &seed);
        fill_weights(l->w_up, hd * dim, &seed);
        fill_weights(l->w_down, dim * hd, &seed);
    }
    fill_norms(&ctx->model);
    ctx->model.loaded = true;

    if (llama_alloc_state(&ctx->state, &ctx->model.config) != 0) return -1;

    /* Init tokenizer with test vocab (same as test_tokenizer.c) */
    static const char *test_vocab[] = {
        "h", "e", "l", "o", " ", "w", "r", "d",
        "he", "wo", "ll", "lo", "hell", "wor", "hello", "worl", "world",
    };
    static const float test_scores[] = {
        0,0,0,0,0,0,0,0, -10,-15,-20,-30,-40,-45,-50,-55,-60
    };
    if (tokenizer_init(&ctx->tokenizer, test_vocab, test_scores, 17, -1, -1) != 0)
        return -1;

    ctx->ready = true;
    return 0;
}

/* ---- Tests ---- */

static void test_init_free(void)
{
    TEST("init + free cycle (no crash)");
    inference_ctx_t ctx;
    int ok = (init_tiny_ctx(&ctx) == 0);
    inference_free(&ctx);
    CHECK(ok, "init_tiny_ctx failed");
}

static void test_query_with_mock(void)
{
    TEST("inference_query with tiny model + test vocab");
    inference_ctx_t ctx;
    if (init_tiny_ctx(&ctx) != 0) { FAIL_MSG("setup failed"); return; }

    char response[256];
    int len = inference_query(&ctx, "hello", response, sizeof(response),
                              4, 0.0f, 1);

    /* Response may be garbage (random weights) but should be non-negative len */
    CHECK(len >= 0, "inference_query returned negative");

    inference_free(&ctx);
}

static void test_query_null_safety(void)
{
    TEST("inference_query null safety");
    inference_ctx_t ctx;
    init_tiny_ctx(&ctx);

    char buf[64];
    int ok = 1;
    if (inference_query(NULL, "hello", buf, 64, 4, 0, 1) != -1) ok = 0;
    if (inference_query(&ctx, NULL, buf, 64, 4, 0, 1) != -1) ok = 0;
    if (inference_query(&ctx, "hello", NULL, 64, 4, 0, 1) != -1) ok = 0;
    if (inference_query(&ctx, "hello", buf, 0, 4, 0, 1) != -1) ok = 0;
    CHECK(ok, "null/zero args should return -1");

    inference_free(&ctx);
}

static void test_query_empty_prompt(void)
{
    TEST("inference_query with empty prompt returns -1");
    inference_ctx_t ctx;
    init_tiny_ctx(&ctx);

    char buf[64];
    int len = inference_query(&ctx, "", buf, sizeof(buf), 4, 0.0f, 1);
    CHECK(len == -1, "empty prompt should return -1");

    inference_free(&ctx);
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS Inference API Tests ===\n\n");

    test_init_free();
    test_query_with_mock();
    test_query_null_safety();
    test_query_empty_prompt();

    printf("\n=== Results: %d/%d PASS ===\n", pass, total);
    return (pass == total) ? 0 : 1;
}
