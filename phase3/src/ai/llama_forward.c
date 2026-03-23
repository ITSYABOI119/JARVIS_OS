/*
 * JARVIS AI-OS Phase 3 — Llama Transformer Forward Pass
 *
 * Full Llama-family transformer: embedding → N layers of
 * (GQA attention + SwiGLU FFN) → output projection.
 * Plus autoregressive generation loop.
 *
 * Pure C11, uses tensor_ops.h for math.
 */

#include "llama_model.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <math.h>
#include <string.h>

#define RMS_EPS 1e-5f

/* ---- Helper: residual add (x += y) ---- */
static void residual_add(float *x, const float *y, int n)
{
    for (int i = 0; i < n; i++)
        x[i] += y[i];
}

/* ---- Forward pass for one token ---- */

void llama_forward(const llama_model_t *model, llama_state_t *state, int token)
{
    const llama_config_t *c = &model->config;
    int dim       = c->dim;
    int n_heads   = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim  = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int kv_dim    = n_kv_heads * head_dim;
    int pos       = state->pos;
    int max_seq   = state->max_seq_len;
    int heads_per_kv = n_heads / n_kv_heads;

    /* 1. EMBEDDING: copy token row into x */
    memcpy(state->x, model->token_embed + token * dim, dim * sizeof(float));

    /* 2. TRANSFORMER LAYERS */
    for (int L = 0; L < c->n_layers; L++) {
        const llama_layer_t *layer = &model->layers[L];

        /* --- Attention --- */

        /* a. RMS norm → xb */
        tensor_rms_norm(state->x, layer->attn_norm, state->xb, dim, RMS_EPS);

        /* b-d. Q/K/V projections (matrix-vector multiply) */
        tensor_matmul(layer->wq, state->xb, state->q, dim, dim, 1);
        tensor_matmul(layer->wk, state->xb, state->k, kv_dim, dim, 1);
        tensor_matmul(layer->wv, state->xb, state->v, kv_dim, dim, 1);

        /* e. RoPE on Q and K */
        for (int h = 0; h < n_heads; h++) {
            for (int i = 0; i < head_dim / 2; i++) {
                float freq = 1.0f / powf(c->rope_theta,
                                          (float)(2 * i) / (float)head_dim);
                float angle = (float)pos * freq;
                float cos_a = cosf(angle);
                float sin_a = sinf(angle);
                int idx = h * head_dim + 2 * i;
                float r0 = state->q[idx], r1 = state->q[idx + 1];
                state->q[idx]     = r0 * cos_a - r1 * sin_a;
                state->q[idx + 1] = r0 * sin_a + r1 * cos_a;
            }
        }
        for (int h = 0; h < n_kv_heads; h++) {
            for (int i = 0; i < head_dim / 2; i++) {
                float freq = 1.0f / powf(c->rope_theta,
                                          (float)(2 * i) / (float)head_dim);
                float angle = (float)pos * freq;
                float cos_a = cosf(angle);
                float sin_a = sinf(angle);
                int idx = h * head_dim + 2 * i;
                float r0 = state->k[idx], r1 = state->k[idx + 1];
                state->k[idx]     = r0 * cos_a - r1 * sin_a;
                state->k[idx + 1] = r0 * sin_a + r1 * cos_a;
            }
        }

        /* f. Store K,V into cache at current position */
        int layer_offset = L * max_seq * kv_dim;
        float *key_layer = state->key_cache + layer_offset;
        float *val_layer = state->value_cache + layer_offset;
        memcpy(key_layer + pos * kv_dim, state->k, kv_dim * sizeof(float));
        memcpy(val_layer + pos * kv_dim, state->v, kv_dim * sizeof(float));

        /* g. Grouped Query Attention */
        for (int h = 0; h < n_heads; h++) {
            int kv_h = h / heads_per_kv;
            float *q_head = state->q + h * head_dim;
            float *att_h  = state->att + h * max_seq;

            /* Score: Q · K^T / sqrt(head_dim) for positions 0..pos */
            for (int t = 0; t <= pos; t++) {
                float *k_t = key_layer + t * kv_dim + kv_h * head_dim;
                float score = 0.0f;
                for (int d = 0; d < head_dim; d++)
                    score += q_head[d] * k_t[d];
                att_h[t] = score / sqrtf((float)head_dim);
            }

            /* Softmax over [0..pos] */
            tensor_softmax(att_h, att_h, pos + 1);

            /* Weighted sum of V → xb */
            float *out_h = state->xb + h * head_dim;
            memset(out_h, 0, head_dim * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float *v_t = val_layer + t * kv_dim + kv_h * head_dim;
                float w = att_h[t];
                for (int d = 0; d < head_dim; d++)
                    out_h[d] += w * v_t[d];
            }
        }

        /* h. Output projection → xb2 */
        tensor_matmul(layer->wo, state->xb, state->xb2, dim, dim, 1);

        /* i. Residual connection */
        residual_add(state->x, state->xb2, dim);

        /* --- Feed-Forward Network (SwiGLU) --- */

        /* j. RMS norm → xb */
        tensor_rms_norm(state->x, layer->ffn_norm, state->xb, dim, RMS_EPS);

        /* k-m. SwiGLU: gate * silu(up) */
        tensor_matmul(layer->w_gate, state->xb, state->hb, hidden_dim, dim, 1);
        tensor_matmul(layer->w_up, state->xb, state->hb2, hidden_dim, dim, 1);
        tensor_silu(state->hb, state->hb, hidden_dim);
        tensor_mul(state->hb, state->hb2, state->hb, hidden_dim);

        /* n. Down projection → xb */
        tensor_matmul(layer->w_down, state->hb, state->xb, dim, hidden_dim, 1);

        /* o. Residual connection */
        residual_add(state->x, state->xb, dim);
    }

    /* 3. Final RMS norm (in-place via xb then copy back) */
    tensor_rms_norm(state->x, model->output_norm, state->xb, dim, RMS_EPS);
    memcpy(state->x, state->xb, dim * sizeof(float));

    /* 4. Output projection → logits */
    tensor_matmul(model->output_weight, state->x, state->logits,
                  c->vocab_size, dim, 1);

    /* 5. Increment position */
    state->pos++;
}

/* ---- Autoregressive Generation ---- */

int llama_generate(const llama_model_t *model, llama_state_t *state,
                   const int *prompt_tokens, int n_prompt,
                   int *output_tokens, int max_output,
                   int eos_token, float temperature, int top_k,
                   uint64_t seed)
{
    /* Reset state */
    state->pos = 0;

    /* Prefill: process all prompt tokens */
    for (int i = 0; i < n_prompt; i++)
        llama_forward(model, state, prompt_tokens[i]);

    /* Autoregressive generation */
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

        llama_forward(model, state, next);
    }
    return generated;
}
