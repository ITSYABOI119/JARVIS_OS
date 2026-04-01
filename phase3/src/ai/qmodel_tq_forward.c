/*
 * JARVIS AI-OS Phase 3 -- Quantized Model + TurboQuant KV Cache Forward Pass
 *
 * Combines quantized weight matmul (llama_quant.c) with TurboQuant compressed
 * KV cache (turboquant.c). The tq_states array is dynamically allocated via
 * calloc to avoid ~4MB fixed arrays on the stack.
 *
 * Pure C11, no C++ dependencies.
 */

#include "qmodel_tq_forward.h"
#include "qmatmul.h"
#include "dequant.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int qmodel_tq_alloc_state(qmodel_tq_state_t *state, const llama_config_t *config,
                           int key_bits, int val_bits, uint64_t base_seed)
{
    if (!state || !config) return -1;
    if (config->dim <= 0 || config->n_layers <= 0) return -1;

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
    state->key_bits    = key_bits;
    state->val_bits    = val_bits;
    state->pos         = 0;

    /* 1. Activation buffers */
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

    if (!state->x || !state->xb || !state->xb2 ||
        !state->q || !state->k || !state->v ||
        !state->att || !state->hb || !state->hb2 || !state->logits) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 2. Compressed KV cache: n_layers * max_seq * n_kv_heads entries */
    size_t cache_entries = (size_t)n_layers * (size_t)max_seq * (size_t)n_kv_heads;
    state->key_cache = (tq_ckey_t *)calloc(cache_entries, sizeof(tq_ckey_t));
    state->val_cache = (tq_cval_t *)calloc(cache_entries, sizeof(tq_cval_t));

    if (!state->key_cache || !state->val_cache) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 3. Per-layer TQ states (dynamically allocated, NOT a fixed array) */
    state->tq_states = (tq_state_t *)calloc((size_t)n_layers, sizeof(tq_state_t));
    if (!state->tq_states) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 4. Initialize each tq_state */
    for (int L = 0; L < n_layers; L++) {
        if (tq_init(&state->tq_states[L], head_dim, key_bits, val_bits,
                    base_seed + (uint64_t)L) != 0) {
            /* tq_init may have partially initialized; free up to L-1 */
            /* Set n_layers to L so free only iterates initialized states */
            state->n_layers = L;
            qmodel_tq_free_state(state);
            return -1;
        }
    }

    /* Restore n_layers after partial-init safeguard */
    state->n_layers = n_layers;
    return 0;
}

void qmodel_tq_free_state(qmodel_tq_state_t *state)
{
    if (!state) return;

    free(state->x);
    free(state->xb);
    free(state->xb2);
    free(state->q);
    free(state->k);
    free(state->v);
    free(state->att);
    free(state->hb);
    free(state->hb2);
    free(state->logits);
    free(state->key_cache);
    free(state->val_cache);

    if (state->tq_states) {
        for (int L = 0; L < state->n_layers; L++)
            tq_free(&state->tq_states[L]);
        free(state->tq_states);
    }

    memset(state, 0, sizeof(*state));
}

void qmodel_tq_print_memory(const qmodel_tq_state_t *state, const llama_config_t *config)
{
    if (!state || !config) return;

    int n_layers   = config->n_layers;
    int n_kv_heads = config->n_kv_heads;
    int head_dim   = config->head_dim;
    int max_seq    = config->max_seq_len;

    size_t cache_entries = (size_t)n_layers * (size_t)max_seq * (size_t)n_kv_heads;

    /* F32 baseline: 2 * n_layers * max_seq * n_kv_heads * head_dim * 4 bytes */
    size_t f32_kv = cache_entries * (size_t)head_dim * 2 * sizeof(float);

    /* TQ compressed: actual struct sizes */
    size_t tq_kv = cache_entries * (sizeof(tq_ckey_t) + sizeof(tq_cval_t));

    /* TQ state overhead (rotation + projection matrices per layer) */
    size_t tq_state_overhead = 0;
    if (state->tq_states) {
        for (int L = 0; L < n_layers; L++)
            tq_state_overhead += tq_state_bytes(&state->tq_states[L]);
    }

    printf("qmodel_tq memory report:\n");
    printf("  F32 KV cache:       %zu MB (%zu bytes)\n",
           f32_kv / (1024 * 1024), f32_kv);
    printf("  TQ KV cache:        %zu KB (%zu bytes)\n",
           tq_kv / 1024, tq_kv);
    printf("  TQ state overhead:  %zu KB (%zu bytes)\n",
           tq_state_overhead / 1024, tq_state_overhead);
    printf("  Compression ratio:  %.1fx\n",
           f32_kv > 0 ? (double)f32_kv / (double)tq_kv : 0.0);
}

#define RMS_EPS 1e-5f

static void residual_add(float *x, const float *y, int n)
{
    for (int i = 0; i < n; i++)
        x[i] += y[i];
}

/* ---- qmodel_tq_forward ---- */

void qmodel_tq_forward(const qmodel_t *qm, qmodel_tq_state_t *state, int token)
{
    const llama_config_t *c = &qm->config;
    int dim        = c->dim;
    int n_heads    = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim   = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int kv_dim     = n_kv_heads * head_dim;
    int pos        = state->pos;
    int max_seq    = state->max_seq_len;
    int heads_per_kv = n_heads / n_kv_heads;

    /* Bounds check token ID (critical on bare-metal seL4 — no memory protection) */
    if (token < 0 || token >= c->vocab_size) {
        memset(state->logits, 0, (size_t)c->vocab_size * sizeof(float));
        state->pos++;
        return;
    }

    /* 1. EMBEDDING: dequantize one row of token_embed into x */
    qembed_lookup(&qm->token_embed, token, state->x, dim);

    /* 2. TRANSFORMER LAYERS */
    for (int L = 0; L < c->n_layers; L++) {
        const qlayer_t *layer = &qm->layers[L];

        /* --- Attention --- */

        /* a. RMS norm -> xb
         *    Norm weights are F32 in the GGUF — cast data pointer directly.
         *    For non-F32 norm weights (unlikely), dequant into xb2 as scratch. */
        if (layer->attn_norm.type == GGML_TYPE_F32) {
            const float *norm = (const float *)layer->attn_norm.data;
            tensor_rms_norm(state->x, norm, state->xb, dim, RMS_EPS);
        } else {
            dequant_row(layer->attn_norm.data, state->xb2, dim,
                        (ggml_type_t)layer->attn_norm.type);
            tensor_rms_norm(state->x, state->xb2, state->xb, dim, RMS_EPS);
        }

        /* b-d. Q/K/V projections (quantized matrix-vector multiply) */
        qmatmul_vec(&layer->wq, state->xb, state->q,  dim,    dim);
        qmatmul_vec(&layer->wk, state->xb, state->k,  kv_dim, dim);
        qmatmul_vec(&layer->wv, state->xb, state->v,  kv_dim, dim);

        /* e. RoPE on Q and K
         * Use qm->rope_freqs if available (custom freqs for extended context) */
        for (int h = 0; h < n_heads; h++) {
            for (int i = 0; i < head_dim / 2; i++) {
                float freq = 1.0f / powf(c->rope_theta, (float)(2 * i) / (float)head_dim);
                if (qm->rope_freqs) freq /= qm->rope_freqs[i];
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
                float freq = 1.0f / powf(c->rope_theta, (float)(2 * i) / (float)head_dim);
                if (qm->rope_freqs) freq /= qm->rope_freqs[i];
                float angle = (float)pos * freq;
                float cos_a = cosf(angle);
                float sin_a = sinf(angle);
                int idx = h * head_dim + 2 * i;
                float r0 = state->k[idx], r1 = state->k[idx + 1];
                state->k[idx]     = r0 * cos_a - r1 * sin_a;
                state->k[idx + 1] = r0 * sin_a + r1 * cos_a;
            }
        }

        /* f. Compress K,V into TQ cache */
        int cache_base = (L * max_seq + pos) * n_kv_heads;
        for (int h = 0; h < n_kv_heads; h++) {
            tq_compress_key(&state->tq_states[L], state->k + h * head_dim,
                            &state->key_cache[cache_base + h]);
            tq_compress_val(&state->tq_states[L], state->v + h * head_dim,
                            &state->val_cache[cache_base + h]);
        }

        /* g. Grouped Query Attention */
        for (int h = 0; h < n_heads; h++) {
            int kv_h = h / heads_per_kv;
            float *q_head = state->q + h * head_dim;
            float *att_h  = state->att + h * max_seq;

            /* Score: Q . K^T / sqrt(head_dim) for positions 0..pos (TQ compressed) */
            for (int t = 0; t <= pos; t++) {
                int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
                float score = tq_dot_key(&state->tq_states[L], q_head,
                                          &state->key_cache[t_idx]);
                att_h[t] = score / sqrtf((float)head_dim);
            }

            /* Softmax over [0..pos] */
            tensor_softmax(att_h, att_h, pos + 1);

            /* Weighted sum of V -> xb (TQ decompressed on the fly) */
            float *out_h = state->xb + h * head_dim;
            memset(out_h, 0, (size_t)head_dim * sizeof(float));
            float tmp_val[TQ_MAX_HEAD_DIM];
            for (int t = 0; t <= pos; t++) {
                int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
                tq_decompress_val(&state->tq_states[L], &state->val_cache[t_idx], tmp_val);
                float w = att_h[t];
                for (int d = 0; d < head_dim; d++)
                    out_h[d] += w * tmp_val[d];
            }
        }

        /* h. Output projection -> xb2 (quantized matmul) */
        qmatmul_vec(&layer->wo, state->xb, state->xb2, dim, dim);

        /* i. Residual connection */
        residual_add(state->x, state->xb2, dim);

        /* --- Feed-Forward Network (SwiGLU) --- */

        /* j. RMS norm -> xb */
        if (layer->ffn_norm.type == GGML_TYPE_F32) {
            const float *norm = (const float *)layer->ffn_norm.data;
            tensor_rms_norm(state->x, norm, state->xb, dim, RMS_EPS);
        } else {
            dequant_row(layer->ffn_norm.data, state->xb2, dim,
                        (ggml_type_t)layer->ffn_norm.type);
            tensor_rms_norm(state->x, state->xb2, state->xb, dim, RMS_EPS);
        }

        /* k-m. SwiGLU: silu(gate(xb)) * up(xb) */
        qmatmul_vec(&layer->w_gate, state->xb, state->hb,  hidden_dim, dim);
        qmatmul_vec(&layer->w_up,   state->xb, state->hb2, hidden_dim, dim);
        tensor_silu(state->hb, state->hb, hidden_dim);
        tensor_mul(state->hb, state->hb2, state->hb, hidden_dim);

        /* n. Down projection -> xb (quantized matmul) */
        qmatmul_vec(&layer->w_down, state->hb, state->xb, dim, hidden_dim);

        /* o. Residual connection */
        residual_add(state->x, state->xb, dim);
    }

    /* 3. Final RMS norm (in-place via xb then copy back) */
    if (qm->output_norm.type == GGML_TYPE_F32) {
        const float *norm = (const float *)qm->output_norm.data;
        tensor_rms_norm(state->x, norm, state->xb, dim, RMS_EPS);
    } else {
        dequant_row(qm->output_norm.data, state->xb2, dim,
                    (ggml_type_t)qm->output_norm.type);
        tensor_rms_norm(state->x, state->xb2, state->xb, dim, RMS_EPS);
    }
    memcpy(state->x, state->xb, (size_t)dim * sizeof(float));

    /* 4. Output projection -> logits (quantized matmul) */
    qmatmul_vec(&qm->output_weight, state->x, state->logits,
                c->vocab_size, dim);

    /* 5. Increment position */
    state->pos++;
}

/* ---- qmodel_tq_generate ---- */

int qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k, uint64_t seed)
{
    /* Reset state */
    state->pos = 0;

    /* Prefill: process all prompt tokens */
    for (int i = 0; i < n_prompt; i++)
        qmodel_tq_forward(qm, state, prompt_tokens[i]);

    /* Autoregressive generation */
    int generated = 0;
    while (generated < max_output && state->pos < state->max_seq_len) {
        int next;
        if (temperature < 1e-6f || top_k <= 1)
            next = sample_greedy(state->logits, qm->config.vocab_size);
        else
            next = sample_topk(state->logits, qm->config.vocab_size,
                               top_k, temperature, &seed);

        output_tokens[generated++] = next;
        if (next == eos_token) break;

        qmodel_tq_forward(qm, state, next);
    }
    return generated;
}
