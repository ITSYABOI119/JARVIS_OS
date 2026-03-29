/*
 * JARVIS AI-OS Phase 3 -- TurboQuant-Integrated Forward Pass
 *
 * Algorithm 2 from arXiv 2504.19874 (TurboQuantProd):
 *   - Compute Q/K/V in F32, apply RoPE in F32
 *   - Compress K,V into tq_ckey_t/tq_cval_t before cache store
 *   - Score attention via tq_dot_key() (QJL-corrected, unbiased)
 *   - Decompress values on-the-fly for weighted sum
 *   - Everything else identical to llama_forward.c
 */

#include "llama_forward_tq.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define RMS_EPS 1e-5f

static void residual_add(float *x, const float *y, int n)
{
    for (int i = 0; i < n; i++)
        x[i] += y[i];
}

int llama_tq_alloc_state(llama_tq_state_t *state, const llama_config_t *config,
                          int key_bits, int val_bits, uint64_t base_seed)
{
    if (!state || !config) return -1;
    if (config->dim <= 0 || config->n_layers <= 0) return -1;
    if (config->n_layers > TQ_MAX_LAYERS) return -1;

    memset(state, 0, sizeof(*state));

    int dim        = config->dim;
    int n_heads    = config->n_heads;
    int n_kv_heads = config->n_kv_heads;
    int head_dim   = config->head_dim;
    int hidden_dim = config->hidden_dim;
    int vocab_size = config->vocab_size;
    int max_seq    = config->max_seq_len;
    int n_layers   = config->n_layers;

    state->max_seq_len = max_seq;
    state->n_layers    = n_layers;
    state->n_kv_heads  = n_kv_heads;
    state->pos         = 0;

    /* Activation buffers (identical to llama_alloc_state) */
    state->x      = (float *)calloc((size_t)dim, sizeof(float));
    state->xb     = (float *)calloc((size_t)dim, sizeof(float));
    state->xb2    = (float *)calloc((size_t)dim, sizeof(float));
    state->q      = (float *)calloc((size_t)n_heads * head_dim, sizeof(float));
    state->k      = (float *)calloc((size_t)n_kv_heads * head_dim, sizeof(float));
    state->v      = (float *)calloc((size_t)n_kv_heads * head_dim, sizeof(float));
    state->att    = (float *)calloc((size_t)n_heads * max_seq, sizeof(float));
    state->hb     = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->hb2    = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->logits = (float *)calloc((size_t)vocab_size, sizeof(float));

    /* Compressed KV cache: n_layers * max_seq * n_kv_heads entries */
    size_t cache_entries = (size_t)n_layers * (size_t)max_seq * (size_t)n_kv_heads;
    state->key_cache = (tq_ckey_t *)calloc(cache_entries, sizeof(tq_ckey_t));
    state->val_cache = (tq_cval_t *)calloc(cache_entries, sizeof(tq_cval_t));

    if (!state->x || !state->xb || !state->xb2 ||
        !state->q || !state->k || !state->v ||
        !state->att || !state->hb || !state->hb2 ||
        !state->logits || !state->key_cache || !state->val_cache) {
        llama_tq_free_state(state);
        return -1;
    }

    /* Init per-layer TQ states */
    for (int L = 0; L < n_layers; L++) {
        if (tq_init(&state->tq_states[L], head_dim, key_bits, val_bits,
                     base_seed + (uint64_t)L) != 0) {
            llama_tq_free_state(state);
            return -1;
        }
    }

    return 0;
}

void llama_tq_free_state(llama_tq_state_t *state)
{
    if (!state) return;
    free(state->x);      free(state->xb);     free(state->xb2);
    free(state->q);      free(state->k);      free(state->v);
    free(state->att);    free(state->hb);     free(state->hb2);
    free(state->logits);
    free(state->key_cache);
    free(state->val_cache);
    for (int L = 0; L < state->n_layers; L++)
        tq_free(&state->tq_states[L]);
    memset(state, 0, sizeof(*state));
}

void llama_tq_forward(const llama_model_t *model, llama_tq_state_t *state, int token)
{
    const llama_config_t *c = &model->config;
    int dim        = c->dim;
    int n_heads    = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim   = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int kv_dim     = n_kv_heads * head_dim;
    int pos        = state->pos;
    int max_seq    = state->max_seq_len;
    int heads_per_kv = n_heads / n_kv_heads;

    if (token < 0 || token >= c->vocab_size) {
        memset(state->logits, 0, (size_t)c->vocab_size * sizeof(float));
        state->pos++;
        return;
    }

    /* 1. EMBEDDING */
    memcpy(state->x, model->token_embed + token * dim, (size_t)dim * sizeof(float));

    /* 2. TRANSFORMER LAYERS */
    for (int L = 0; L < c->n_layers; L++) {
        const llama_layer_t *layer = &model->layers[L];
        tq_state_t *tqs = &state->tq_states[L];

        /* a. RMS norm */
        tensor_rms_norm(state->x, layer->attn_norm, state->xb, dim, RMS_EPS);

        /* b-d. Q/K/V projections */
        tensor_matmul(layer->wq, state->xb, state->q, dim, dim, 1);
        tensor_matmul(layer->wk, state->xb, state->k, kv_dim, dim, 1);
        tensor_matmul(layer->wv, state->xb, state->v, kv_dim, dim, 1);

        /* e. RoPE on Q and K (in F32, BEFORE compression) */
        for (int h = 0; h < n_heads; h++) {
            for (int i = 0; i < head_dim / 2; i++) {
                float freq = 1.0f / powf(c->rope_theta, (float)(2 * i) / (float)head_dim);
                if (model->rope_freqs) freq /= model->rope_freqs[i];
                float angle = (float)pos * freq;
                float cos_a = cosf(angle), sin_a = sinf(angle);
                int idx = h * head_dim + 2 * i;
                float r0 = state->q[idx], r1 = state->q[idx + 1];
                state->q[idx]     = r0 * cos_a - r1 * sin_a;
                state->q[idx + 1] = r0 * sin_a + r1 * cos_a;
            }
        }
        for (int h = 0; h < n_kv_heads; h++) {
            for (int i = 0; i < head_dim / 2; i++) {
                float freq = 1.0f / powf(c->rope_theta, (float)(2 * i) / (float)head_dim);
                if (model->rope_freqs) freq /= model->rope_freqs[i];
                float angle = (float)pos * freq;
                float cos_a = cosf(angle), sin_a = sinf(angle);
                int idx = h * head_dim + 2 * i;
                float r0 = state->k[idx], r1 = state->k[idx + 1];
                state->k[idx]     = r0 * cos_a - r1 * sin_a;
                state->k[idx + 1] = r0 * sin_a + r1 * cos_a;
            }
        }

        /* f. CHANGED: Compress K,V into TQ cache at current position */
        int cache_base = (L * max_seq + pos) * n_kv_heads;
        for (int h = 0; h < n_kv_heads; h++) {
            tq_compress_key(tqs, state->k + h * head_dim,
                            &state->key_cache[cache_base + h]);
            tq_compress_val(tqs, state->v + h * head_dim,
                            &state->val_cache[cache_base + h]);
        }

        /* g. Grouped Query Attention (CHANGED: TQ scoring + on-the-fly decompress) */
        for (int h = 0; h < n_heads; h++) {
            int kv_h = h / heads_per_kv;
            float *q_head = state->q + h * head_dim;
            float *att_h  = state->att + h * max_seq;

            /* CHANGED: Score via tq_dot_key (QJL-corrected) */
            for (int t = 0; t <= pos; t++) {
                int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
                float score = tq_dot_key(tqs, q_head, &state->key_cache[t_idx]);
                att_h[t] = score / sqrtf((float)head_dim);
            }

            /* Softmax over [0..pos] (unchanged) */
            tensor_softmax(att_h, att_h, pos + 1);

            /* CHANGED: Weighted sum with on-the-fly value decompression */
            float *out_h = state->xb + h * head_dim;
            memset(out_h, 0, (size_t)head_dim * sizeof(float));
            float tmp_val[TQ_MAX_HEAD_DIM];
            for (int t = 0; t <= pos; t++) {
                int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
                tq_decompress_val(tqs, &state->val_cache[t_idx], tmp_val);
                float w = att_h[t];
                for (int d = 0; d < head_dim; d++)
                    out_h[d] += w * tmp_val[d];
            }
        }

        /* h. Output projection (unchanged) */
        tensor_matmul(layer->wo, state->xb, state->xb2, dim, dim, 1);

        /* i. Residual */
        residual_add(state->x, state->xb2, dim);

        /* j. FFN RMS norm */
        tensor_rms_norm(state->x, layer->ffn_norm, state->xb, dim, RMS_EPS);

        /* k-m. SwiGLU */
        tensor_matmul(layer->w_gate, state->xb, state->hb, hidden_dim, dim, 1);
        tensor_matmul(layer->w_up, state->xb, state->hb2, hidden_dim, dim, 1);
        tensor_silu(state->hb, state->hb, hidden_dim);
        tensor_mul(state->hb, state->hb2, state->hb, hidden_dim);

        /* n. Down projection */
        tensor_matmul(layer->w_down, state->hb, state->xb, dim, hidden_dim, 1);

        /* o. Residual */
        residual_add(state->x, state->xb, dim);
    }

    /* 3. Final RMS norm */
    tensor_rms_norm(state->x, model->output_norm, state->xb, dim, RMS_EPS);
    memcpy(state->x, state->xb, (size_t)dim * sizeof(float));

    /* 4. Output projection -> logits */
    tensor_matmul(model->output_weight, state->x, state->logits,
                  c->vocab_size, dim, 1);

    /* 5. Increment position */
    state->pos++;
}

int llama_tq_generate(const llama_model_t *model, llama_tq_state_t *state,
                       const int *prompt_tokens, int n_prompt,
                       int *output_tokens, int max_output,
                       int eos_token, float temperature, int top_k,
                       uint64_t seed)
{
    state->pos = 0;

    for (int i = 0; i < n_prompt; i++)
        llama_tq_forward(model, state, prompt_tokens[i]);

    int generated = 0;
    while (generated < max_output && state->pos < state->max_seq_len) {
        int next;
        if (temperature < 1e-6f || top_k <= 1)
            next = sample_greedy(state->logits, model->config.vocab_size);
        else
            next = sample_topk(state->logits, model->config.vocab_size,
                               top_k, temperature, &seed);

        output_tokens[generated++] = next;
        if (next == eos_token) break;

        llama_tq_forward(model, state, next);
    }
    return generated;
}
