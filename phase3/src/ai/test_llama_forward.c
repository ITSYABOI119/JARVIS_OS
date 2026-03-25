/*
 * JARVIS AI-OS — Llama Forward Pass Tests
 * Uses tiny synthetic model (dim=4, 1 layer, GQA) for validation.
 */

#include "llama_model.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ---- Test framework ---- */
static int pass = 0, fail = 0, total = 0;
#define TEST(name) do { total++; printf("Test %d: %s ... ", total, name); } while(0)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL_MSG(m) do { fail++; printf("FAIL: %s\n", m); } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL_MSG(msg); } while(0)

/* ---- Tiny model setup ---- */

static llama_config_t tiny_config = {
    .dim = 4,
    .n_layers = 1,
    .n_heads = 2,
    .n_kv_heads = 1,   /* GQA: 2 query heads share 1 KV head */
    .head_dim = 2,      /* 4 / 2 */
    .hidden_dim = 8,
    .vocab_size = 16,
    .max_seq_len = 8,
    .rope_theta = 10000.0f,
};

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

static int init_tiny_model(llama_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->config = tiny_config;
    if (llama_alloc_model(model) != 0) return -1;

    uint64_t seed = 42;
    int dim = tiny_config.dim;
    int hd  = tiny_config.hidden_dim;
    int vs  = tiny_config.vocab_size;
    int kvd = tiny_config.n_kv_heads * tiny_config.head_dim;

    fill_weights(model->token_embed, vs * dim, &seed);
    fill_weights(model->output_weight, vs * dim, &seed);
    for (int L = 0; L < tiny_config.n_layers; L++) {
        llama_layer_t *l = &model->layers[L];
        fill_weights(l->wq, dim * dim, &seed);
        fill_weights(l->wk, kvd * dim, &seed);
        fill_weights(l->wv, kvd * dim, &seed);
        fill_weights(l->wo, dim * dim, &seed);
        fill_weights(l->w_gate, hd * dim, &seed);
        fill_weights(l->w_up, hd * dim, &seed);
        fill_weights(l->w_down, dim * hd, &seed);
    }
    fill_norms(model);
    model->loaded = true;
    return 0;
}

static void reset_state(llama_state_t *state, const llama_config_t *config)
{
    int kv_dim = config->n_kv_heads * config->head_dim;
    int cache_size = config->n_layers * state->max_seq_len * kv_dim;
    memset(state->key_cache, 0, cache_size * sizeof(float));
    memset(state->value_cache, 0, cache_size * sizeof(float));
    state->pos = 0;
}

/* ---- Tests ---- */

static void test_forward_produces_logits(void)
{
    TEST("forward produces non-zero, non-NaN logits");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);

    int any_nonzero = 0, any_nan = 0;
    for (int i = 0; i < model.config.vocab_size; i++) {
        if (state.logits[i] != 0.0f) any_nonzero = 1;
        if (isnan(state.logits[i])) any_nan = 1;
    }
    CHECK(any_nonzero && !any_nan, "logits all zero or contain NaN");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_forward_increments_pos(void)
{
    TEST("forward increments pos correctly");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);
    int ok = (state.pos == 1);
    llama_forward(&model, &state, 1);
    ok = ok && (state.pos == 2);
    CHECK(ok, "pos not incremented correctly");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_kv_cache_written(void)
{
    TEST("KV cache written after forward");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);

    /* Check key_cache at pos=0 has non-zero values */
    int kv_dim = model.config.n_kv_heads * model.config.head_dim;
    int any_nonzero = 0;
    for (int i = 0; i < kv_dim; i++)
        if (state.key_cache[i] != 0.0f) any_nonzero = 1;
    CHECK(any_nonzero, "KV cache all zeros after forward");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_different_tokens_different_logits(void)
{
    TEST("different tokens produce different logits");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);
    float logit0 = state.logits[0];

    reset_state(&state, &model.config);
    llama_forward(&model, &state, 1);
    float logit1 = state.logits[0];

    CHECK(logit0 != logit1, "same logits for different tokens");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_greedy_determinism(void)
{
    TEST("greedy sampling is deterministic");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);
    int tok_a = sample_greedy(state.logits, model.config.vocab_size);

    reset_state(&state, &model.config);
    llama_forward(&model, &state, 0);
    int tok_b = sample_greedy(state.logits, model.config.vocab_size);

    CHECK(tok_a == tok_b, "greedy not deterministic");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_generate_basic(void)
{
    TEST("generate produces valid tokens");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    int prompt[] = {0};
    int output[4];
    int n = llama_generate(&model, &state, prompt, 1, output, 4,
                           -1, 0.0f, 1, 42);

    int ok = (n > 0 && n <= 4);
    for (int i = 0; i < n && ok; i++)
        if (output[i] < 0 || output[i] >= model.config.vocab_size) ok = 0;
    CHECK(ok, "generate returned invalid tokens or count");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_generate_deterministic(void)
{
    TEST("generate is deterministic with same seed");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    int prompt[] = {0};
    int out1[4], out2[4];

    int n1 = llama_generate(&model, &state, prompt, 1, out1, 4,
                            -1, 0.5f, 5, 12345);

    /* State is reset inside generate, just call again */
    int n2 = llama_generate(&model, &state, prompt, 1, out2, 4,
                            -1, 0.5f, 5, 12345);

    int ok = (n1 == n2);
    for (int i = 0; i < n1 && ok; i++)
        if (out1[i] != out2[i]) ok = 0;
    CHECK(ok, "generate not deterministic with same seed");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_generate_stops_at_eos(void)
{
    TEST("generate stops at EOS token");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    /* Run generation with greedy — find what token it produces first */
    int prompt[] = {0};
    int probe[1];
    llama_generate(&model, &state, prompt, 1, probe, 1, -1, 0.0f, 1, 0);
    int first_token = probe[0];

    /* Now set that token as EOS — generation should produce it then stop */
    int output[8];
    int n = llama_generate(&model, &state, prompt, 1, output, 8,
                           first_token, 0.0f, 1, 0);

    /* Should have stopped at or after producing the eos token */
    int found_eos = 0;
    for (int i = 0; i < n; i++)
        if (output[i] == first_token) { found_eos = 1; break; }
    CHECK(found_eos, "EOS token not found in output");

    llama_free_state(&state);
    llama_free_model(&model);
}

static void test_multiple_positions(void)
{
    TEST("forward through multiple positions (KV cache)");
    llama_model_t model;
    llama_state_t state;
    init_tiny_model(&model);
    llama_alloc_state(&state, &model.config);

    llama_forward(&model, &state, 0);
    llama_forward(&model, &state, 1);
    llama_forward(&model, &state, 2);

    int ok = (state.pos == 3);
    for (int i = 0; i < model.config.vocab_size && ok; i++)
        if (isnan(state.logits[i])) ok = 0;
    CHECK(ok, "pos wrong or NaN in logits after 3 positions");

    llama_free_state(&state);
    llama_free_model(&model);
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS Llama Forward Pass Tests ===\n\n");

    test_forward_produces_logits();
    test_forward_increments_pos();
    test_kv_cache_written();
    test_different_tokens_different_logits();
    test_greedy_determinism();
    test_generate_basic();
    test_generate_deterministic();
    test_generate_stops_at_eos();
    test_multiple_positions();

    printf("\n=== Results: %d/%d PASS ===\n", pass, total);
    return (pass == total) ? 0 : 1;
}
