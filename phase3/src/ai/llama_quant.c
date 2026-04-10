/*
 * JARVIS AI-OS Phase 3 — Quantized Llama Model (Zero-Copy)
 *
 * Keeps weights in their quantized form inside embedded GGUF .rodata.
 * Dequantizes on-the-fly during matrix-vector multiply, one block at a time
 * (256 floats = 1 KB on stack). Total heap for a Llama 3.2 1B Q4_K_M model:
 * ~50 MB instead of 5.7 GB.
 *
 * Tensor name mapping (Llama GGUF convention):
 *   token_embd.weight             -> token_embed
 *   output_norm.weight            -> output_norm
 *   output.weight                 -> output_weight
 *   blk.N.attn_norm.weight        -> layers[N].attn_norm
 *   blk.N.attn_q.weight           -> layers[N].wq
 *   blk.N.attn_k.weight           -> layers[N].wk
 *   blk.N.attn_v.weight           -> layers[N].wv
 *   blk.N.attn_output.weight      -> layers[N].wo
 *   blk.N.ffn_norm.weight         -> layers[N].ffn_norm
 *   blk.N.ffn_gate.weight         -> layers[N].w_gate
 *   blk.N.ffn_up.weight           -> layers[N].w_up
 *   blk.N.ffn_down.weight         -> layers[N].w_down
 *
 * Pure C11, no C++ dependencies.
 */

#include "llama_quant.h"
#include "dequant.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#define RMS_EPS 1e-5f

/* ============================================================
 * Internal helpers
 * ============================================================ */

/* ---- Resolve a GGUF tensor to a zero-copy qtensor_t ---- */

static int resolve_qtensor(qtensor_t *qt, const gguf_ctx_t *ctx,
                           const void *gguf_base, const char *name)
{
    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
    if (!t) {
        fprintf(stderr, "qmodel_load: tensor '%s' not found\n", name);
        return -1;
    }

    qt->data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
    qt->type       = t->type;
    qt->n_elements = t->n_elements;
    qt->n_bytes    = t->n_bytes;
    return 0;
}

/* ---- Quantized matrix-vector multiply ----
 *
 * W is [M x K] in quantized form, x is [K] F32, out is [M] F32.
 * Dequantizes one block at a time (max 256 floats = 1 KB on stack)
 * to avoid allocating a full K-element temp row.
 *
 * For F32 weights (rare — only used if a weight tensor happens to be F32),
 * we fall through to a direct dot product without dequant overhead.
 */
static void qmatmul_vec(const qtensor_t *W, const float *x, float *out,
                         int M, int K)
{
    const ggml_type_t wtype = (ggml_type_t)W->type;

    /* F32 fast path: direct dot product, no dequant needed */
    if (wtype == GGML_TYPE_F32) {
        const float *wf = (const float *)W->data;
        for (int i = 0; i < M; i++) {
            const float *row = wf + (size_t)i * K;
            float dot = 0.0f;
            for (int j = 0; j < K; j++)
                dot += row[j] * x[j];
            out[i] = dot;
        }
        return;
    }

    /* Quantized path: dequant one block at a time */
    const int   block_size  = dequant_type_block_size(wtype);
    const size_t block_bytes = dequant_type_block_bytes(wtype);

    if (block_size <= 0 || block_bytes == 0) {
        /* Unsupported type — zero output as safe fallback */
        memset(out, 0, (size_t)M * sizeof(float));
        return;
    }

    const size_t row_bytes = dequant_row_bytes(K, wtype);
    const int    n_blocks  = K / block_size;
    const uint8_t *wdata   = (const uint8_t *)W->data;

    /* Stack buffer for one dequantized block (max 256 for Q4_K/Q6_K) */
    float tmp[256];

    for (int i = 0; i < M; i++) {
        const uint8_t *block_ptr = wdata + (size_t)i * row_bytes;
        float dot = 0.0f;

        for (int b = 0; b < n_blocks; b++) {
            dequant_row(block_ptr, tmp, block_size, wtype);

            const float *xb = x + b * block_size;
#ifdef __AVX2__
            {
                __m256 acc = _mm256_setzero_ps();
                int j = 0;
                for (; j + 7 < block_size; j += 8)
                    acc = _mm256_fmadd_ps(_mm256_loadu_ps(tmp + j),
                                           _mm256_loadu_ps(xb + j), acc);
                __m128 hi = _mm256_extractf128_ps(acc, 1);
                __m128 lo = _mm256_castps256_ps128(acc);
                __m128 s = _mm_add_ps(lo, hi);
                s = _mm_hadd_ps(s, s);
                s = _mm_hadd_ps(s, s);
                dot += _mm_cvtss_f32(s);
                for (; j < block_size; j++)
                    dot += tmp[j] * xb[j];
            }
#else
            int j = 0;
            for (; j + 3 < block_size; j += 4)
                dot += tmp[j]*xb[j] + tmp[j+1]*xb[j+1] + tmp[j+2]*xb[j+2] + tmp[j+3]*xb[j+3];
            for (; j < block_size; j++)
                dot += tmp[j] * xb[j];
#endif

            block_ptr += block_bytes;
        }

        out[i] = dot;
    }
}

/* ---- Quantized embedding lookup ----
 *
 * Embedding is [vocab_size x dim] in quantized form (typically Q6_K).
 * Extracts and dequantizes a single row for the given token ID.
 */
static void qembed_lookup(const qtensor_t *embed, int token, float *out,
                           int dim)
{
    const ggml_type_t etype = (ggml_type_t)embed->type;

    /* F32 fast path */
    if (etype == GGML_TYPE_F32) {
        const float *ef = (const float *)embed->data;
        memcpy(out, ef + (size_t)token * dim, (size_t)dim * sizeof(float));
        return;
    }

    /* Quantized: compute row offset and dequant */
    const size_t row_bytes = dequant_row_bytes(dim, etype);
    const void *row = (const uint8_t *)embed->data + (size_t)token * row_bytes;
    dequant_row(row, out, dim, etype);
}

/* ---- Residual add (x += y) ---- */

static void residual_add(float *x, const float *y, int n)
{
    for (int i = 0; i < n; i++)
        x[i] += y[i];
}

/* ---- Logit softcapping (Gemma 4) ----
 * Applies cap * tanh(x / cap) to prevent extreme logit values. */
static void logit_softcap(float *logits, int n, float cap)
{
    if (cap <= 0.0f) return;
    float inv_cap = 1.0f / cap;
    for (int i = 0; i < n; i++)
        logits[i] = cap * tanhf(logits[i] * inv_cap);
}

/* ============================================================
 * Public API
 * ============================================================ */

/* ---- qmodel_load ---- */

int qmodel_load(qmodel_t *qm, const gguf_ctx_t *ctx, const void *gguf_base)
{
    if (!qm || !ctx || !gguf_base) return -1;

    memset(qm, 0, sizeof(*qm));

    /* Reuse existing config loader from llama_load.c */
    int err = llama_load_config(&qm->config, ctx);
    if (err) {
        fprintf(stderr, "qmodel_load: config load failed\n");
        return err;
    }

    const llama_config_t *c = &qm->config;

    /* Allocate only the layers array — tiny: n_layers * sizeof(qlayer_t) */
    qm->layers = (qlayer_t *)calloc((size_t)c->n_layers, sizeof(qlayer_t));
    if (!qm->layers) {
        fprintf(stderr, "qmodel_load: layers alloc failed\n");
        return -1;
    }

    /* Resolve global tensors (zero-copy pointers into gguf_base + data_offset) */
    err = resolve_qtensor(&qm->token_embed, ctx, gguf_base, "token_embd.weight");
    if (err) goto fail;

    err = resolve_qtensor(&qm->output_norm, ctx, gguf_base, "output_norm.weight");
    if (err) goto fail;

    /* output.weight may not exist — Llama 3.2 1B uses weight tying
     * (output projection shares token_embd.weight) */
    {
        const gguf_tensor_info_t *out_t = gguf_find_tensor(ctx, "output.weight");
        if (out_t) {
            qm->output_weight.data       = (const uint8_t *)gguf_base + ctx->data_offset + out_t->offset;
            qm->output_weight.type       = out_t->type;
            qm->output_weight.n_elements = out_t->n_elements;
            qm->output_weight.n_bytes    = out_t->n_bytes;
        } else {
            /* Weight tying: reuse token embedding for output projection */
            qm->output_weight = qm->token_embed;
        }
    }

    /* RoPE frequencies (optional — custom freqs for extended context models) */
    {
        const gguf_tensor_info_t *rf = gguf_find_tensor(ctx, "rope_freqs.weight");
        if (rf && rf->type == GGML_TYPE_F32 &&
            rf->n_elements == (uint64_t)(c->head_dim / 2)) {
            qm->rope_freqs = (const float *)((const uint8_t *)gguf_base +
                              ctx->data_offset + rf->offset);
        }
    }

    /* Resolve per-layer tensors */
    char name[128];
    for (int i = 0; i < c->n_layers; i++) {
        qlayer_t *L = &qm->layers[i];

        snprintf(name, sizeof(name), "blk.%d.attn_norm.weight", i);
        err = resolve_qtensor(&L->attn_norm, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", i);
        err = resolve_qtensor(&L->wq, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
        err = resolve_qtensor(&L->wk, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
        err = resolve_qtensor(&L->wv, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", i);
        err = resolve_qtensor(&L->wo, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_norm.weight", i);
        err = resolve_qtensor(&L->ffn_norm, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_gate.weight", i);
        err = resolve_qtensor(&L->w_gate, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", i);
        err = resolve_qtensor(&L->w_up, ctx, gguf_base, name);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_down.weight", i);
        err = resolve_qtensor(&L->w_down, ctx, gguf_base, name);
        if (err) goto fail;
    }

    qm->loaded = true;
    return 0;

fail:
    qmodel_free(qm);
    return -1;
}

/* ---- qmodel_free ---- */

void qmodel_free(qmodel_t *qm)
{
    if (!qm) return;

    /* Only the layers array was malloc'd — weight data lives in .rodata */
    free(qm->layers);
    llama_free_config(&qm->config);
    memset(qm, 0, sizeof(*qm));
}

/* ---- qmodel_forward ---- */

void qmodel_forward(const qmodel_t *qm, llama_state_t *state, int token)
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

        /* f. Store K,V into cache at current position */
        int layer_offset = L * max_seq * kv_dim;
        float *key_layer = state->key_cache + layer_offset;
        float *val_layer = state->value_cache + layer_offset;
        memcpy(key_layer + pos * kv_dim, state->k, (size_t)kv_dim * sizeof(float));
        memcpy(val_layer + pos * kv_dim, state->v, (size_t)kv_dim * sizeof(float));

        /* g. Grouped Query Attention */
        for (int h = 0; h < n_heads; h++) {
            int kv_h = h / heads_per_kv;
            float *q_head = state->q + h * head_dim;
            float *att_h  = state->att + h * max_seq;

            /* Score: Q . K^T / sqrt(head_dim) for positions 0..pos */
            for (int t = 0; t <= pos; t++) {
                float *k_t = key_layer + t * kv_dim + kv_h * head_dim;
                float score = 0.0f;
                for (int d = 0; d < head_dim; d++)
                    score += q_head[d] * k_t[d];
                att_h[t] = score / sqrtf((float)head_dim);
            }

            /* Softmax over [0..pos] */
            tensor_softmax(att_h, att_h, pos + 1);

            /* Weighted sum of V -> xb */
            float *out_h = state->xb + h * head_dim;
            memset(out_h, 0, (size_t)head_dim * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float *v_t = val_layer + t * kv_dim + kv_h * head_dim;
                float w = att_h[t];
                for (int d = 0; d < head_dim; d++)
                    out_h[d] += w * v_t[d];
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

    /* 4b. Logit softcapping (Gemma 4) */
    if (c->logit_softcap > 0.0f)
        logit_softcap(state->logits, c->vocab_size, c->logit_softcap);

    /* 5. Increment position */
    state->pos++;
}

/* ---- qmodel_generate ---- */

int qmodel_generate(const qmodel_t *qm, llama_state_t *state,
                    const int *prompt_tokens, int n_prompt,
                    int *output_tokens, int max_output,
                    int eos_token, float temperature, int top_k,
                    uint64_t seed)
{
    /* Reset state */
    state->pos = 0;

    /* Prefill: process all prompt tokens */
    for (int i = 0; i < n_prompt; i++)
        qmodel_forward(qm, state, prompt_tokens[i]);

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

        qmodel_forward(qm, state, next);
    }
    return generated;
}
