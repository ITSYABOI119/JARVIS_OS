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
 * Gemma 4 feature-disable flags for debugging
 * Set to 1 to disable a feature. Start with all 1, then enable
 * one at a time to find which feature breaks generation.
 * ============================================================ */
/* === DEBUGGING: Set to 1 to disable, 0 to enable ===
 * All features RE-ENABLED after finding root cause: missing embed_scale.
 * Keep flags for future debugging if needed. */
/* ALL DISABLED again — testing embed_scale fix in vanilla path */
#define G4_DISABLE_PLE          1  /* skip PLE residual add */
#define G4_DISABLE_SANDWICH     1  /* skip post_attn_norm, post_ffw_norm */
#define G4_DISABLE_SCALE        1  /* skip layer_output_scale */
#define G4_DISABLE_QK_NORM      1  /* skip per-head Q/K RMSNorm */
#define G4_DISABLE_DUAL_ROPE    1  /* use single rope_theta for all layers */
#define G4_DISABLE_SOFTCAP      1  /* skip logit softcapping */

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

/* ---- Per-head RMSNorm (Gemma 4, Qwen3) ----
 * Normalizes each head independently using RMSNorm:
 *   head[j] = head[j] / RMS(head) * weight[j]
 * Applied to Q and K projections BEFORE RoPE rotation. */
static void per_head_rms_norm(float *x, const float *weight, int n_heads,
                              int head_dim, float eps)
{
    for (int h = 0; h < n_heads; h++) {
        float *head = x + h * head_dim;
        float ss = 0.0f;
        for (int j = 0; j < head_dim; j++)
            ss += head[j] * head[j];
        ss = 1.0f / sqrtf(ss / (float)head_dim + eps);
        for (int j = 0; j < head_dim; j++)
            head[j] = head[j] * ss * weight[j];
    }
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

    /* PLE global tensors (optional — Gemma 4 only, skip if not present) */
    if (c->ple_dim > 0) {
        const gguf_tensor_info_t *t;

        t = gguf_find_tensor(ctx, "per_layer_token_embd.weight");
        if (t) {
            qm->ple_embed.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
            qm->ple_embed.type       = t->type;
            qm->ple_embed.n_elements = t->n_elements;
            qm->ple_embed.n_bytes    = t->n_bytes;
        }

        t = gguf_find_tensor(ctx, "per_layer_model_proj.weight");
        if (t) {
            qm->ple_proj.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
            qm->ple_proj.type       = t->type;
            qm->ple_proj.n_elements = t->n_elements;
            qm->ple_proj.n_bytes    = t->n_bytes;
        }

        t = gguf_find_tensor(ctx, "per_layer_proj_norm.weight");
        if (t) {
            qm->ple_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
            qm->ple_norm.type       = t->type;
            qm->ple_norm.n_elements = t->n_elements;
            qm->ple_norm.n_bytes    = t->n_bytes;
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

        /* K/V weights: required for layers that compute their own KV,
         * optional for shared-KV layers (they reuse another layer's cache).
         * In Gemma 4 E2B, shared layers have NO wk/wv tensors in the GGUF. */
        {
            int is_shared = (c->kv_share_map && c->kv_share_map[i] >= 0);
            snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
            if (is_shared) {
                const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                if (t) {
                    L->wk.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->wk.type       = t->type;
                    L->wk.n_elements = t->n_elements;
                    L->wk.n_bytes    = t->n_bytes;
                }
                /* else: wk stays zeroed (NULL data) — this layer shares KV */
            } else {
                err = resolve_qtensor(&L->wk, ctx, gguf_base, name);
                if (err) goto fail;
            }

            snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
            if (is_shared) {
                const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                if (t) {
                    L->wv.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->wv.type       = t->type;
                    L->wv.n_elements = t->n_elements;
                    L->wv.n_bytes    = t->n_bytes;
                }
                /* else: wv stays zeroed (NULL data) — this layer shares KV */
            } else {
                err = resolve_qtensor(&L->wv, ctx, gguf_base, name);
                if (err) goto fail;
            }
        }

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

        /* Per-head Q/K RMSNorm tensors (optional — Gemma 4, Qwen3) */
        snprintf(name, sizeof(name), "blk.%d.attn_q_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->q_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->q_norm.type       = t->type;
                L->q_norm.n_elements = t->n_elements;
                L->q_norm.n_bytes    = t->n_bytes;
            }
        }
        snprintf(name, sizeof(name), "blk.%d.attn_k_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->k_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->k_norm.type       = t->type;
                L->k_norm.n_elements = t->n_elements;
                L->k_norm.n_bytes    = t->n_bytes;
            }
        }

        /* Sandwich norm tensors (optional — Gemma 4) */
        snprintf(name, sizeof(name), "blk.%d.post_attention_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->post_attn_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->post_attn_norm.type       = t->type;
                L->post_attn_norm.n_elements = t->n_elements;
                L->post_attn_norm.n_bytes    = t->n_bytes;
            }
        }

        snprintf(name, sizeof(name), "blk.%d.post_ffw_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->post_ffw_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->post_ffw_norm.type       = t->type;
                L->post_ffw_norm.n_elements = t->n_elements;
                L->post_ffw_norm.n_bytes    = t->n_bytes;
            }
        }

        snprintf(name, sizeof(name), "blk.%d.layer_output_scale.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->layer_output_scale.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->layer_output_scale.type       = t->type;
                L->layer_output_scale.n_elements = t->n_elements;
                L->layer_output_scale.n_bytes    = t->n_bytes;
            }
        }

        /* PLE per-layer tensors (optional — Gemma 4) */
        if (c->ple_dim > 0) {
            snprintf(name, sizeof(name), "blk.%d.inp_gate.weight", i);
            {
                const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                if (t) {
                    L->ple_gate.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ple_gate.type       = t->type;
                    L->ple_gate.n_elements = t->n_elements;
                    L->ple_gate.n_bytes    = t->n_bytes;
                }
            }

            snprintf(name, sizeof(name), "blk.%d.proj.weight", i);
            {
                const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                if (t) {
                    L->ple_proj.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ple_proj.type       = t->type;
                    L->ple_proj.n_elements = t->n_elements;
                    L->ple_proj.n_bytes    = t->n_bytes;
                }
            }

            snprintf(name, sizeof(name), "blk.%d.post_norm.weight", i);
            {
                const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                if (t) {
                    L->post_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->post_norm.type       = t->type;
                    L->post_norm.n_elements = t->n_elements;
                    L->post_norm.n_bytes    = t->n_bytes;
                }
            }
        }
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
    int hidden_dim = c->hidden_dim;
    int pos        = state->pos;
    int max_seq    = state->max_seq_len;
    int heads_per_kv = n_heads / n_kv_heads;

    /* Pre-compute max dims for KV cache addressing.
     * For Llama: head_dim_swa is 0, max_head_dim == head_dim (unchanged). */
    int max_head_dim = c->head_dim;
    if (c->head_dim_swa > max_head_dim) max_head_dim = c->head_dim_swa;
    int max_kv_dim = n_kv_heads * max_head_dim;

    /* Bounds check token ID (critical on bare-metal seL4 — no memory protection) */
    if (token < 0 || token >= c->vocab_size) {
        memset(state->logits, 0, (size_t)c->vocab_size * sizeof(float));
        state->pos++;
        return;
    }

    /* 1. EMBEDDING: dequantize one row of token_embed into x */
    qembed_lookup(&qm->token_embed, token, state->x, dim);

    /* Gemma embedding scaling: multiply by sqrt(dim) after lookup */
    if (c->embed_scale) {
        float scale = sqrtf((float)dim);
        for (int i = 0; i < dim; i++)
            state->x[i] *= scale;
    }

    /* PLE: compute per-layer embedding (once per token, reused by all layers)
     * Stack arrays sized for max ple_dim=256 and max dim=4096. */
    float ple_normed[256]; /* ple_dim (max 256 for Gemma 4 E2B) */
    int has_ple = (!G4_DISABLE_PLE && c->ple_dim > 0 && qm->ple_embed.data != NULL);
    if (has_ple) {
        /* Lookup PLE embedding for this token */
        float ple_raw[256];
        qembed_lookup(&qm->ple_embed, token, ple_raw, c->ple_dim);

        /* RMSNorm the PLE embedding */
        float norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
        if (qm->ple_norm.data) {
            if (qm->ple_norm.type == GGML_TYPE_F32) {
                tensor_rms_norm(ple_raw, (const float *)qm->ple_norm.data,
                                ple_normed, c->ple_dim, norm_eps);
            } else {
                float norm_w[256];
                dequant_row(qm->ple_norm.data, norm_w, c->ple_dim,
                            (ggml_type_t)qm->ple_norm.type);
                tensor_rms_norm(ple_raw, norm_w, ple_normed, c->ple_dim, norm_eps);
            }
        } else {
            memcpy(ple_normed, ple_raw, (size_t)c->ple_dim * sizeof(float));
        }
    }

    /* 2. TRANSFORMER LAYERS */
    for (int L = 0; L < c->n_layers; L++) {
        const qlayer_t *layer = &qm->layers[L];

        /* PLE: gated residual add — x += GELU(gate) * proj(ple_normed)
         * Applied at start of each layer, before attention. */
        if (has_ple && layer->ple_proj.data && layer->ple_gate.data) {
            /* Project PLE from ple_dim to dim: ple_out = proj @ ple_normed */
            float ple_out[4096]; /* dim (max 4096) */
            qmatmul_vec(&layer->ple_proj, ple_normed, ple_out, dim, c->ple_dim);

            /* Dequant gate weights */
            float gate[4096]; /* dim (max 4096) */
            if (layer->ple_gate.type == GGML_TYPE_F32) {
                memcpy(gate, layer->ple_gate.data, (size_t)dim * sizeof(float));
            } else {
                dequant_row(layer->ple_gate.data, gate, dim,
                            (ggml_type_t)layer->ple_gate.type);
            }

            /* Apply GELU to gate, multiply with ple_out, add to x */
            for (int d = 0; d < dim; d++) {
                float g = gate[d];
                /* GELU: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715*x^3))) */
                float g3 = g * g * g;
                g = 0.5f * g * (1.0f + tanhf(0.7978845608f * (g + 0.044715f * g3)));
                state->x[d] += g * ple_out[d];
            }
        }

        /* --- Attention --- */

        /* Per-layer attention dimensions:
         * SWA layers use head_dim_swa; global layers use head_dim.
         * For Llama: layer_is_swa is NULL, is_swa is always 0 -> all layers use head_dim. */
        int is_swa = (c->layer_is_swa && c->layer_is_swa[L]);
        int l_head_dim = (is_swa && c->head_dim_swa > 0) ? c->head_dim_swa : c->head_dim;
        int l_kv_dim   = n_kv_heads * l_head_dim;
        int l_q_dim    = n_heads * l_head_dim;

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

        /* Determine KV source layer.
         * Shared KV: layers with kv_share_map[L] >= 0 skip K/V projection
         * entirely and reuse K/V from the source layer's cache.
         * For Llama: kv_share_map is NULL -> every layer computes own KV. */
        int kv_src = L;       /* default: own KV */
        int has_own_kv = 1;
        if (c->kv_share_map && c->kv_share_map[L] >= 0) {
            kv_src = c->kv_share_map[L];
            has_own_kv = 0;
        }

        float norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
        float l_rope_theta = (!G4_DISABLE_DUAL_ROPE && is_swa && c->rope_theta_swa > 0.0f)
                             ? c->rope_theta_swa : c->rope_theta;

        if (has_own_kv) {
            /* b-d. Normal: Q/K/V projections with PER-LAYER output dims.
             *    Q: [l_q_dim x dim],  K/V: [l_kv_dim x dim]
             *    For Llama: l_q_dim == dim, l_kv_dim == kv_dim (unchanged). */
            qmatmul_vec(&layer->wq, state->xb, state->q,  l_q_dim,  dim);
            qmatmul_vec(&layer->wk, state->xb, state->k,  l_kv_dim, dim);
            qmatmul_vec(&layer->wv, state->xb, state->v,  l_kv_dim, dim);

            /* Per-head Q/K RMSNorm (Gemma 4, Qwen3) — applied BEFORE RoPE.
             * Normalizes each head independently: head[j] = head[j] / RMS(head) * weight[j].
             * For Llama: q_norm.data/k_norm.data are NULL -> skipped. */
            if (!G4_DISABLE_QK_NORM && layer->q_norm.data) {
                float qn[512]; /* l_head_dim, max 512 */
                if (layer->q_norm.type == GGML_TYPE_F32)
                    memcpy(qn, layer->q_norm.data, (size_t)l_head_dim * sizeof(float));
                else
                    dequant_row(layer->q_norm.data, qn, l_head_dim,
                                (ggml_type_t)layer->q_norm.type);
                per_head_rms_norm(state->q, qn, n_heads, l_head_dim, norm_eps);
            }
            if (!G4_DISABLE_QK_NORM && layer->k_norm.data) {
                float kn[512]; /* l_head_dim, max 512 */
                if (layer->k_norm.type == GGML_TYPE_F32)
                    memcpy(kn, layer->k_norm.data, (size_t)l_head_dim * sizeof(float));
                else
                    dequant_row(layer->k_norm.data, kn, l_head_dim,
                                (ggml_type_t)layer->k_norm.type);
                per_head_rms_norm(state->k, kn, n_kv_heads, l_head_dim, norm_eps);
            }

            /* e. RoPE on Q AND K — uses l_head_dim (256 for SWA, 512 for global).
             * Dual theta: SWA layers use rope_theta_swa, global use rope_theta. */
            for (int h = 0; h < n_heads; h++) {
                for (int i = 0; i < l_head_dim / 2; i++) {
                    float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                    if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                    float angle = (float)pos * freq;
                    float cos_a = cosf(angle);
                    float sin_a = sinf(angle);
                    int idx = h * l_head_dim + 2 * i;
                    float r0 = state->q[idx], r1 = state->q[idx + 1];
                    state->q[idx]     = r0 * cos_a - r1 * sin_a;
                    state->q[idx + 1] = r0 * sin_a + r1 * cos_a;
                }
            }
            for (int h = 0; h < n_kv_heads; h++) {
                for (int i = 0; i < l_head_dim / 2; i++) {
                    float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                    if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                    float angle = (float)pos * freq;
                    float cos_a = cosf(angle);
                    float sin_a = sinf(angle);
                    int idx = h * l_head_dim + 2 * i;
                    float r0 = state->k[idx], r1 = state->k[idx + 1];
                    state->k[idx]     = r0 * cos_a - r1 * sin_a;
                    state->k[idx + 1] = r0 * sin_a + r1 * cos_a;
                }
            }

            /* f. Store K,V into cache at current position.
             *    Cache stride uses max_kv_dim for uniform addressing.
             *    SWA layers (smaller l_kv_dim) only fill a prefix of each slot. */
            {
                int layer_offset = L * max_seq * max_kv_dim;
                float *kl = state->key_cache + layer_offset;
                float *vl = state->value_cache + layer_offset;
                memcpy(kl + pos * max_kv_dim, state->k, (size_t)l_kv_dim * sizeof(float));
                memcpy(vl + pos * max_kv_dim, state->v, (size_t)l_kv_dim * sizeof(float));
            }
        } else {
            /* Shared KV: compute ONLY Q (K/V come from source layer's cache).
             * K was already normed + RoPE'd when the source layer computed it. */
            qmatmul_vec(&layer->wq, state->xb, state->q, l_q_dim, dim);

            /* Q norm only (K was already normed when source layer computed it) */
            if (!G4_DISABLE_QK_NORM && layer->q_norm.data) {
                float qn[512];
                if (layer->q_norm.type == GGML_TYPE_F32)
                    memcpy(qn, layer->q_norm.data, (size_t)l_head_dim * sizeof(float));
                else
                    dequant_row(layer->q_norm.data, qn, l_head_dim,
                                (ggml_type_t)layer->q_norm.type);
                per_head_rms_norm(state->q, qn, n_heads, l_head_dim, norm_eps);
            }

            /* RoPE on Q only (K already has RoPE from source layer) */
            for (int h = 0; h < n_heads; h++) {
                for (int i = 0; i < l_head_dim / 2; i++) {
                    float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                    if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                    float angle = (float)pos * freq;
                    float cos_a = cosf(angle);
                    float sin_a = sinf(angle);
                    int idx = h * l_head_dim + 2 * i;
                    float r0 = state->q[idx], r1 = state->q[idx + 1];
                    state->q[idx]     = r0 * cos_a - r1 * sin_a;
                    state->q[idx + 1] = r0 * sin_a + r1 * cos_a;
                }
            }
            /* No K/V store — reuse source layer's cache */
        }

        /* g. Grouped Query Attention — read KV from source layer's cache.
         *    For own-KV layers: kv_src == L (reads own cache, just stored above).
         *    For shared layers: kv_src is the source layer index.
         *    SWA layers only attend to the last swa_window positions.
         *    Global layers attend to all positions [0..pos] (att_start=0).
         *
         * NOTE: The kv_share_map from Task 2 uses i % n_unique, which naturally
         * preserves SWA/global type matching in Gemma 4 E2B because the SWA pattern
         * repeats with the same period. Q and K therefore have the same head_dim.
         * TODO: Add explicit attention-type matching verification for other models. */
        int kv_layer_offset = kv_src * max_seq * max_kv_dim;
        float *key_layer = state->key_cache + kv_layer_offset;
        float *val_layer = state->value_cache + kv_layer_offset;

        int att_start = 0;
        if (is_swa && c->swa_window > 0)
            att_start = (pos >= c->swa_window) ? (pos - c->swa_window + 1) : 0;

        for (int h = 0; h < n_heads; h++) {
            int kv_h = h / heads_per_kv;
            float *q_head = state->q + h * l_head_dim;
            float *att_h  = state->att + h * max_seq;

            /* Score: Q . K^T / sqrt(l_head_dim) for positions [att_start..pos] */
            for (int t = att_start; t <= pos; t++) {
                float *k_t = key_layer + t * max_kv_dim + kv_h * l_head_dim;
                float score = 0.0f;
                for (int d = 0; d < l_head_dim; d++)
                    score += q_head[d] * k_t[d];
                att_h[t] = score / sqrtf((float)l_head_dim);
            }

            /* Softmax over [att_start..pos] */
            tensor_softmax(att_h + att_start, att_h + att_start, pos + 1 - att_start);

            /* Weighted sum of V -> xb (using l_head_dim per head) */
            float *out_h = state->xb + h * l_head_dim;
            memset(out_h, 0, (size_t)l_head_dim * sizeof(float));
            for (int t = att_start; t <= pos; t++) {
                float *v_t = val_layer + t * max_kv_dim + kv_h * l_head_dim;
                float w = att_h[t];
                for (int d = 0; d < l_head_dim; d++)
                    out_h[d] += w * v_t[d];
            }
        }

        /* h. Output projection -> xb2 (quantized matmul)
         *    Wo maps [n_heads * l_head_dim] -> [dim].
         *    For Llama: n_heads * head_dim == dim, so this is [dim, dim] (unchanged). */
        qmatmul_vec(&layer->wo, state->xb, state->xb2, dim, l_q_dim);

        /* Post-attention RMSNorm (Gemma 4 sandwich norm) */
        if (!G4_DISABLE_SANDWICH && layer->post_attn_norm.data) {
            float norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
            if (layer->post_attn_norm.type == GGML_TYPE_F32) {
                const float *norm = (const float *)layer->post_attn_norm.data;
                tensor_rms_norm(state->xb2, norm, state->xb2, dim, norm_eps);
            } else {
                float norm_buf[4096];
                dequant_row(layer->post_attn_norm.data, norm_buf, dim,
                            (ggml_type_t)layer->post_attn_norm.type);
                tensor_rms_norm(state->xb2, norm_buf, state->xb2, dim, norm_eps);
            }
        }

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

        /* k-m. SwiGLU: silu(gate(xb)) * up(xb)
         *    Per-layer FFN hidden dim for variable-FFN models (Gemma 4).
         *    For Llama: layer_ffn_dim is NULL -> l_ffn_dim == hidden_dim (unchanged). */
        int l_ffn_dim = (c->layer_ffn_dim) ? c->layer_ffn_dim[L] : hidden_dim;
        qmatmul_vec(&layer->w_gate, state->xb, state->hb,  l_ffn_dim, dim);
        qmatmul_vec(&layer->w_up,   state->xb, state->hb2, l_ffn_dim, dim);
        tensor_silu(state->hb, state->hb, l_ffn_dim);
        tensor_mul(state->hb, state->hb2, state->hb, l_ffn_dim);

        /* n. Down projection -> xb (quantized matmul) */
        qmatmul_vec(&layer->w_down, state->hb, state->xb, dim, l_ffn_dim);

        /* Post-FFN RMSNorm (Gemma 4 sandwich norm) */
        if (!G4_DISABLE_SANDWICH && layer->post_ffw_norm.data) {
            float norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
            if (layer->post_ffw_norm.type == GGML_TYPE_F32) {
                const float *norm = (const float *)layer->post_ffw_norm.data;
                tensor_rms_norm(state->xb, norm, state->xb, dim, norm_eps);
            } else {
                float norm_buf[4096];
                dequant_row(layer->post_ffw_norm.data, norm_buf, dim,
                            (ggml_type_t)layer->post_ffw_norm.type);
                tensor_rms_norm(state->xb, norm_buf, state->xb, dim, norm_eps);
            }
        }

        /* o. Residual connection */
        residual_add(state->x, state->xb, dim);

        /* Layer output scaling (Gemma 4) */
        if (!G4_DISABLE_SCALE && layer->layer_output_scale.data) {
            if (layer->layer_output_scale.type == GGML_TYPE_F32) {
                const float *scale = (const float *)layer->layer_output_scale.data;
                tensor_mul(state->x, scale, state->x, dim);
            } else {
                float scale_buf[4096];
                dequant_row(layer->layer_output_scale.data, scale_buf, dim,
                            (ggml_type_t)layer->layer_output_scale.type);
                tensor_mul(state->x, scale_buf, state->x, dim);
            }
        }
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
    if (!G4_DISABLE_SOFTCAP && c->logit_softcap > 0.0f)
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
