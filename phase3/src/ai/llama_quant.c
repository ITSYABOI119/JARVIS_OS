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
#include "qdot.h"
#include "threadpool.h"
#include "tensor_ops.h"
#include "sampling.h"
#include "ssm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

/* Per-forward-pass debug tracing — gates [L00@N], [FWD], [TOP5@N] diagnostics.
 * Default is OFF (silent) for performance; pass -DJARVIS_DBG_FORWARD=1 on the
 * compiler command line to re-enable. Also documented in jarvis_debug.h. */
#ifndef JARVIS_DBG_FORWARD
#define JARVIS_DBG_FORWARD 0
#endif

#define RMS_EPS 1e-5f

#ifdef JARVIS_PTHREAD
typedef struct { const float *wf; const float *x; float *out; int K; } qmatmul_f32_ctx_t;
typedef struct { const uint8_t *wdata; size_t row_bytes; const float *x; float *out; int K; ggml_type_t wtype; } qmatmul_qdot_ctx_t;
_Static_assert(sizeof(qmatmul_f32_ctx_t) <= 64, "ctx must fit threadpool ctx_buf[64]");
_Static_assert(sizeof(qmatmul_qdot_ctx_t) <= 64, "ctx must fit threadpool ctx_buf[64]");

static void qmatmul_f32_row(int idx, void *p);
static void qmatmul_qdot_row(int idx, void *p);
#endif

/* ============================================================
 * Gemma 4 feature-disable flags for debugging
 * Set to 1 to disable a feature. Start with all 1, then enable
 * one at a time to find which feature breaks generation.
 * ============================================================ */
/* All Gemma 4 features enabled — confirmed ESSENTIAL by norm explosion test.
 * Without layer_output_scale (0.0178), |x| grows from 100 -> 4.5M across 35 layers. */
#define G4_DISABLE_PLE          0
#define G4_DISABLE_SANDWICH     0
#define G4_DISABLE_SCALE        0
#define G4_DISABLE_QK_NORM      0
#define G4_DISABLE_DUAL_ROPE    0
#define G4_DISABLE_SOFTCAP      0

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

    const int use_threads =
#ifdef JARVIS_PTHREAD
        (jarvis_threads() > 1 && M >= 256);
#else
        0;
#endif

    /* F32 fast path: direct dot product, no dequant needed */
    if (wtype == GGML_TYPE_F32) {
        const float *wf = (const float *)W->data;
        if (use_threads) {
#ifdef JARVIS_PTHREAD
            qmatmul_f32_ctx_t ctx = { wf, x, out, K };
            jarvis_parallel_for(0, M, qmatmul_f32_row, &ctx, sizeof(ctx));
#else
            for (int i = 0; i < M; i++) {
                const float *row = wf + (size_t)i * K;
                float dot = 0.0f;
                for (int j = 0; j < K; j++)
                    dot += row[j] * x[j];
                out[i] = dot;
            }
#endif
        } else {
            for (int i = 0; i < M; i++) {
                const float *row = wf + (size_t)i * K;
                float dot = 0.0f;
                for (int j = 0; j < K; j++)
                    dot += row[j] * x[j];
                out[i] = dot;
            }
        }
        return;
    }

    /* Quantized path: fused dequant-dot (one call per row) */
    const size_t row_bytes = dequant_row_bytes(K, wtype);
    if (row_bytes == 0) {
        memset(out, 0, (size_t)M * sizeof(float));
        return;
    }
    const uint8_t *wdata = (const uint8_t *)W->data;

    if (use_threads) {
#ifdef JARVIS_PTHREAD
        qmatmul_qdot_ctx_t ctx = { wdata, row_bytes, x, out, K, wtype };
        jarvis_parallel_for(0, M, qmatmul_qdot_row, &ctx, sizeof(ctx));
#else
        for (int i = 0; i < M; i++)
            out[i] = qdot_row(wdata + (size_t)i * row_bytes, x, K, wtype);
#endif
    } else {
        for (int i = 0; i < M; i++)
            out[i] = qdot_row(wdata + (size_t)i * row_bytes, x, K, wtype);
    }
}

#ifdef JARVIS_PTHREAD
static void qmatmul_f32_row(int idx, void *p)
{
    qmatmul_f32_ctx_t *c = (qmatmul_f32_ctx_t *)p;
    const float *row = c->wf + (size_t)idx * (size_t)c->K;
    float dot = 0.0f;
    for (int j = 0; j < c->K; j++)
        dot += row[j] * c->x[j];
    c->out[idx] = dot;
}

static void qmatmul_qdot_row(int idx, void *p)
{
    qmatmul_qdot_ctx_t *c = (qmatmul_qdot_ctx_t *)p;
    c->out[idx] = qdot_row(c->wdata + (size_t)idx * c->row_bytes, c->x, c->K, c->wtype);
}
#endif

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

#ifdef __AVX2__
static inline float dot_f32_avx2(const float *a, const float *b, int n)
{
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();

    int i = 0;
    for (; i + 31 < n; i += 32) {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i),       _mm256_loadu_ps(b + i),       sum0);
        sum1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8),   _mm256_loadu_ps(b + i + 8),   sum1);
        sum2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 16),  _mm256_loadu_ps(b + i + 16),  sum2);
        sum3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 24),  _mm256_loadu_ps(b + i + 24),  sum3);
    }
    __m256 sum = _mm256_add_ps(_mm256_add_ps(sum0, sum1), _mm256_add_ps(sum2, sum3));
    for (; i + 7 < n; i += 8)
        sum = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), sum);

    __m128 hi = _mm256_extractf128_ps(sum, 1);
    __m128 lo = _mm256_castps256_ps128(sum);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    float dot = _mm_cvtss_f32(s);
    for (; i < n; i++)
        dot += a[i] * b[i];
    return dot;
}

static inline void accum_axpy_f32_avx2(float *dst, const float *src, float w, int n)
{
    __m256 vw = _mm256_set1_ps(w);
    int i = 0;
    for (; i + 31 < n; i += 32) {
        __m256 d0 = _mm256_loadu_ps(dst + i);
        __m256 d1 = _mm256_loadu_ps(dst + i + 8);
        __m256 d2 = _mm256_loadu_ps(dst + i + 16);
        __m256 d3 = _mm256_loadu_ps(dst + i + 24);
        d0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + i),      vw, d0);
        d1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 8),  vw, d1);
        d2 = _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 16), vw, d2);
        d3 = _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 24), vw, d3);
        _mm256_storeu_ps(dst + i,      d0);
        _mm256_storeu_ps(dst + i + 8,  d1);
        _mm256_storeu_ps(dst + i + 16, d2);
        _mm256_storeu_ps(dst + i + 24, d3);
    }
    for (; i + 7 < n; i += 8) {
        __m256 d = _mm256_loadu_ps(dst + i);
        d = _mm256_fmadd_ps(_mm256_loadu_ps(src + i), vw, d);
        _mm256_storeu_ps(dst + i, d);
    }
    for (; i < n; i++)
        dst[i] += w * src[i];
}
#endif

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

/* rope_ensure_tables() — defined in llama_load.c, declared in llama_model.h */

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

        /* Q/K/V weights — try separate tensors first, fall back to fused QKV.
         * Fused layout: [Q_rows || K_rows || V_rows] concatenated vertically.
         * Split at load time into three qtensor_t views — forward pass unchanged. */
        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", i);
        {
            const gguf_tensor_info_t *tq = gguf_find_tensor(ctx, name);
            if (tq) {
                /* --- Separate Q/K/V path (Llama, Gemma, Mistral) --- */
                L->wq.data       = (const uint8_t *)gguf_base + ctx->data_offset + tq->offset;
                L->wq.type       = tq->type;
                L->wq.n_elements = tq->n_elements;
                L->wq.n_bytes    = tq->n_bytes;

                /* K/V weights: optional — shared-KV layers may lack wk/wv tensors.
                 * Presence/absence is used below to derive the KV share map. */
                snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
                {
                    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                    if (t) {
                        L->wk.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                        L->wk.type       = t->type;
                        L->wk.n_elements = t->n_elements;
                        L->wk.n_bytes    = t->n_bytes;
                    }
                }
                snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
                {
                    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
                    if (t) {
                        L->wv.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                        L->wv.type       = t->type;
                        L->wv.n_elements = t->n_elements;
                        L->wv.n_bytes    = t->n_bytes;
                    }
                }
            } else {
                /* --- Fused QKV path (Phi-3, Qwen3.5, Falcon) --- */
                snprintf(name, sizeof(name), "blk.%d.attn_qkv.weight", i);
                const gguf_tensor_info_t *tqkv = gguf_find_tensor(ctx, name);
                if (!tqkv) {
                    printf("[qmodel] layer %d: neither attn_q nor attn_qkv found\n", i);
                    goto fail;
                }

                int q_rows  = c->n_heads * c->head_dim;
                int kv_rows = c->n_kv_heads * c->head_dim;
                uint64_t expected = (uint64_t)(q_rows + 2 * kv_rows) * (uint64_t)c->dim;

                if (tqkv->n_elements != expected && c->ssm_d_state > 0) {
                    /* Try SSM dimensions: Q=n_group*d_state, K=same, V=d_inner */
                    q_rows  = c->ssm_n_group * c->ssm_d_state;
                    kv_rows = q_rows;  /* K same as Q for DeltaNet */
                    int v_rows = c->ssm_d_inner;
                    expected = (uint64_t)(q_rows + kv_rows + v_rows) * (uint64_t)c->dim;
                    if (tqkv->n_elements == expected) {
                        /* SSM QKV: Q[q_rows] + K[kv_rows] + V[v_rows] */
                        size_t row_bytes = dequant_row_bytes(c->dim, (ggml_type_t)tqkv->type);
                        const uint8_t *fused = (const uint8_t *)gguf_base +
                                               ctx->data_offset + tqkv->offset;
                        L->wq.data = fused;
                        L->wq.type = tqkv->type;
                        L->wq.n_elements = (uint64_t)q_rows * c->dim;
                        L->wq.n_bytes = (uint64_t)q_rows * row_bytes;
                        L->wk.data = fused + (size_t)q_rows * row_bytes;
                        L->wk.type = tqkv->type;
                        L->wk.n_elements = (uint64_t)kv_rows * c->dim;
                        L->wk.n_bytes = (uint64_t)kv_rows * row_bytes;
                        L->wv.data = fused + (size_t)(q_rows + kv_rows) * row_bytes;
                        L->wv.type = tqkv->type;
                        L->wv.n_elements = (uint64_t)v_rows * c->dim;
                        L->wv.n_bytes = (uint64_t)v_rows * row_bytes;
                        if (i == 0) {
                            printf("[qmodel] SSM fused QKV: q=%d k=%d v=%d rows, type=%s\n",
                                   q_rows, kv_rows, v_rows,
                                   dequant_type_name((ggml_type_t)tqkv->type));
                        }
                    } else {
                        printf("[qmodel] layer %d: fused QKV n_elements=%llu, expected %llu\n",
                               i, (unsigned long long)tqkv->n_elements, (unsigned long long)expected);
                        goto fail;
                    }
                } else if (tqkv->n_elements != expected) {
                    printf("[qmodel] layer %d: fused QKV n_elements=%llu, expected %llu "
                           "(q=%d + 2*kv=%d rows, dim=%d)\n",
                           i, (unsigned long long)tqkv->n_elements,
                           (unsigned long long)expected, q_rows, kv_rows, c->dim);
                    goto fail;
                } else {
                    /* Standard fused QKV split (Phi-3, Falcon) */
                    size_t row_bytes = dequant_row_bytes(c->dim, (ggml_type_t)tqkv->type);
                    const uint8_t *fused = (const uint8_t *)gguf_base +
                                           ctx->data_offset + tqkv->offset;

                    /* Q: rows [0, q_rows) */
                    L->wq.data       = fused;
                    L->wq.type       = tqkv->type;
                    L->wq.n_elements = (uint64_t)q_rows * c->dim;
                    L->wq.n_bytes    = (uint64_t)q_rows * row_bytes;

                    /* K: rows [q_rows, q_rows + kv_rows) */
                    L->wk.data       = fused + (size_t)q_rows * row_bytes;
                    L->wk.type       = tqkv->type;
                    L->wk.n_elements = (uint64_t)kv_rows * c->dim;
                    L->wk.n_bytes    = (uint64_t)kv_rows * row_bytes;

                    /* V: rows [q_rows + kv_rows, q_rows + 2*kv_rows) */
                    L->wv.data       = fused + (size_t)(q_rows + kv_rows) * row_bytes;
                    L->wv.type       = tqkv->type;
                    L->wv.n_elements = (uint64_t)kv_rows * c->dim;
                    L->wv.n_bytes    = (uint64_t)kv_rows * row_bytes;

                    if (i == 0) {
                        printf("[qmodel] Fused QKV detected: q=%d k=%d v=%d rows, "
                               "row_bytes=%zu, type=%s\n",
                               q_rows, kv_rows, kv_rows, row_bytes,
                               dequant_type_name((ggml_type_t)tqkv->type));
                    }
                }
            }
        }

        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->wo.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->wo.type       = t->type;
                L->wo.n_elements = t->n_elements;
                L->wo.n_bytes    = t->n_bytes;
            }
            /* DeltaNet layers use ssm_out instead — wo.data stays NULL, checked in forward pass */
        }

        snprintf(name, sizeof(name), "blk.%d.ffn_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->ffn_norm.data       = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->ffn_norm.type       = t->type;
                L->ffn_norm.n_elements = t->n_elements;
                L->ffn_norm.n_bytes    = t->n_bytes;
            }
            /* Qwen3.5 aliasing handled after all tensors loaded (below) */
        }

        /* FFN gate/up weights — try separate tensors first, fall back to fused gate-up.
         * Fused layout (Phi-3): ffn_up has 2x hidden_dim rows = [gate || up] concatenated.
         * Split at load time into w_gate and w_up views — forward pass unchanged. */
        snprintf(name, sizeof(name), "blk.%d.ffn_gate.weight", i);
        {
            const gguf_tensor_info_t *tgate = gguf_find_tensor(ctx, name);
            if (tgate) {
                /* --- Separate gate + up (Llama, Gemma, Mistral) --- */
                L->w_gate.data       = (const uint8_t *)gguf_base + ctx->data_offset + tgate->offset;
                L->w_gate.type       = tgate->type;
                L->w_gate.n_elements = tgate->n_elements;
                L->w_gate.n_bytes    = tgate->n_bytes;

                snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", i);
                err = resolve_qtensor(&L->w_up, ctx, gguf_base, name);
                if (err) goto fail;
            } else {
                /* --- Fused gate-up (Phi-3): ffn_up = [gate_rows || up_rows] --- */
                snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", i);
                const gguf_tensor_info_t *tup = gguf_find_tensor(ctx, name);
                if (!tup) {
                    printf("[qmodel] layer %d: neither ffn_gate nor ffn_up found\n", i);
                    goto fail;
                }

                /* Fused tensor has 2*hidden_dim rows: gate first, then up */
                int half_rows = c->hidden_dim;
                uint64_t expected_fused = (uint64_t)(2 * half_rows) * (uint64_t)c->dim;
                if (tup->n_elements != expected_fused) {
                    printf("[qmodel] layer %d: ffn_up n_elements=%llu, expected %llu "
                           "for fused gate-up (2*%d rows, dim=%d)\n",
                           i, (unsigned long long)tup->n_elements,
                           (unsigned long long)expected_fused, half_rows, c->dim);
                    goto fail;
                }

                size_t row_bytes = dequant_row_bytes(c->dim, (ggml_type_t)tup->type);
                const uint8_t *fused = (const uint8_t *)gguf_base +
                                       ctx->data_offset + tup->offset;

                /* Gate: first half_rows rows */
                L->w_gate.data       = fused;
                L->w_gate.type       = tup->type;
                L->w_gate.n_elements = (uint64_t)half_rows * c->dim;
                L->w_gate.n_bytes    = (uint64_t)half_rows * row_bytes;

                /* Up: second half_rows rows */
                L->w_up.data       = fused + (size_t)half_rows * row_bytes;
                L->w_up.type       = tup->type;
                L->w_up.n_elements = (uint64_t)half_rows * c->dim;
                L->w_up.n_bytes    = (uint64_t)half_rows * row_bytes;

                if (i == 0) {
                    printf("[qmodel] Fused gate-up detected: %d + %d rows, "
                           "row_bytes=%zu, type=%s\n",
                           half_rows, half_rows, row_bytes,
                           dequant_type_name((ggml_type_t)tup->type));
                }
            }
        }

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

        /* DeltaNet/SSM tensors (optional — Qwen3.5, NULL for non-DeltaNet layers) */
        snprintf(name, sizeof(name), "blk.%d.ssm_conv1d.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_conv1d.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_conv1d.type = t->type; L->ssm_conv1d.n_elements = t->n_elements; L->ssm_conv1d.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_alpha.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_alpha.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_alpha.type = t->type; L->ssm_alpha.n_elements = t->n_elements; L->ssm_alpha.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_beta.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_beta.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_beta.type = t->type; L->ssm_beta.n_elements = t->n_elements; L->ssm_beta.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_a", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_a.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_a.type = t->type; L->ssm_a.n_elements = t->n_elements; L->ssm_a.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_dt.bias", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_dt_bias.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_dt_bias.type = t->type; L->ssm_dt_bias.n_elements = t->n_elements; L->ssm_dt_bias.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_norm.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_norm.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_norm.type = t->type; L->ssm_norm.n_elements = t->n_elements; L->ssm_norm.n_bytes = t->n_bytes; } }

        snprintf(name, sizeof(name), "blk.%d.ssm_out.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->ssm_out.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->ssm_out.type = t->type; L->ssm_out.n_elements = t->n_elements; L->ssm_out.n_bytes = t->n_bytes; } }

        /* Output gate (DeltaNet Z gate or full-attention sigmoid gate) */
        snprintf(name, sizeof(name), "blk.%d.attn_gate.weight", i);
        { const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
          if (t) { L->attn_out_gate.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                    L->attn_out_gate.type = t->type; L->attn_out_gate.n_elements = t->n_elements; L->attn_out_gate.n_bytes = t->n_bytes; } }

        /* Qwen3.5: post_attention_norm serves as pre-FFN norm (no separate ffn_norm tensor).
         * Alias it so the forward pass uses ffn_norm uniformly.
         * Must run AFTER both ffn_norm and post_attn_norm loading attempts. */
        if (!L->ffn_norm.data && L->post_attn_norm.data) {
            L->ffn_norm = L->post_attn_norm;
            L->post_attn_norm.data = NULL; /* clear so sandwich norm step is a no-op */
        }
    }

    /* --- Derive per-layer arrays from actual tensor shapes ---
     * Replaces heuristics in llama_load_config() with ground truth.
     * Only runs for models with per-layer variation (swa_window > 0).
     * Uses qm->config (non-const) since we're updating derived fields. */
    {
        llama_config_t *cfg = &qm->config;  /* non-const for derivation */
        if (cfg->swa_window > 0 || cfg->shared_kv_layers > 0) {
            /* Free any heuristic arrays from llama_load_config, replace with derived */
            free(cfg->layer_is_swa);
            free(cfg->layer_ffn_dim);
            free(cfg->kv_share_map);
            cfg->layer_is_swa = (bool *)calloc((size_t)cfg->n_layers, sizeof(bool));
            cfg->layer_ffn_dim = (int *)calloc((size_t)cfg->n_layers, sizeof(int));
            cfg->kv_share_map = (int *)malloc((size_t)cfg->n_layers * sizeof(int));
            if (!cfg->layer_is_swa || !cfg->layer_ffn_dim || !cfg->kv_share_map)
                goto fail;

            int max_ffn = 0;
            for (int i = 0; i < cfg->n_layers; i++) {
                qlayer_t *L = &qm->layers[i];

                /* FFN dim from gate weight shape: w_gate is [ffn_dim x dim] */
                if (L->w_gate.n_elements > 0 && cfg->dim > 0)
                    cfg->layer_ffn_dim[i] = (int)(L->w_gate.n_elements / (uint64_t)cfg->dim);
                else
                    cfg->layer_ffn_dim[i] = cfg->hidden_dim;
                if (cfg->layer_ffn_dim[i] > max_ffn)
                    max_ffn = cfg->layer_ffn_dim[i];

                /* SWA flag from wq shape: wq is [n_heads * head_dim x dim]
                 * If derived head_dim matches head_dim_swa, it's an SWA layer. */
                if (L->wq.n_elements > 0 && cfg->dim > 0 && cfg->n_heads > 0) {
                    int actual_q_out = (int)(L->wq.n_elements / (uint64_t)cfg->dim);
                    int actual_head_dim = actual_q_out / cfg->n_heads;
                    cfg->layer_is_swa[i] = (cfg->head_dim_swa > 0 &&
                                             actual_head_dim == cfg->head_dim_swa);
                }

                /* Shared KV: use config's shared_kv_layers value (NOT tensor presence).
                 * The GGUF has wk/wv tensors for ALL layers but llama.cpp IGNORES them
                 * for shared layers. llama.cpp's actual behavior:
                 *   - Layers 0..(n_layers-shared_kv_layers-1): own K/V
                 *   - Layers (n_layers-shared_kv_layers)..(n_layers-1): reuse from
                 *     the most recent same-SWA-type layer with own K/V.
                 */
                int n_unique_kv = cfg->n_layers - cfg->shared_kv_layers;
                if (n_unique_kv <= 0) n_unique_kv = cfg->n_layers;
                if (i < n_unique_kv) {
                    cfg->kv_share_map[i] = -1;  /* own KV */
                } else {
                    /* Find most recent layer in [0, n_unique_kv) with same SWA type */
                    bool my_swa = cfg->layer_is_swa[i];
                    int source = -1;
                    for (int j = n_unique_kv - 1; j >= 0; j--) {
                        if (cfg->layer_is_swa[j] == my_swa) {
                            source = j;
                            break;
                        }
                    }
                    cfg->kv_share_map[i] = (source >= 0) ? source : 0;
                }
            }

            /* Update hidden_dim if derived max exceeds heuristic */
            if (max_ffn > cfg->hidden_dim) {
                printf("[WARN] derived max_ffn=%d > hidden_dim=%d\n", max_ffn, cfg->hidden_dim);
                cfg->hidden_dim = max_ffn;
            }

            /* Print derived values for debugging — show ALL layers to verify KV sharing */
            printf("[qmodel] Derived per-layer dims from tensor shapes:\n");
            for (int i = 0; i < cfg->n_layers; i++) {
                printf("  layer %2d: swa=%d ffn=%5d kv_share=%d",
                       i, cfg->layer_is_swa[i], cfg->layer_ffn_dim[i], cfg->kv_share_map[i]);
                if (cfg->kv_share_map[i] >= 0)
                    printf(" (from L%d)", cfg->kv_share_map[i]);
                printf("\n");
            }
            printf("  hidden_dim=%d\n", cfg->hidden_dim);
        }
    }

    /* Derive DeltaNet layer map from tensor presence */
    if (c->ssm_d_conv > 0) {
        llama_config_t *cfg = &qm->config;
        if (!cfg->layer_is_deltanet) {
            cfg->layer_is_deltanet = (bool *)calloc((size_t)cfg->n_layers, sizeof(bool));
            if (!cfg->layer_is_deltanet) goto fail;
        }
        int n_dn = 0, n_attn = 0;
        for (int i = 0; i < cfg->n_layers; i++) {
            cfg->layer_is_deltanet[i] = (qm->layers[i].ssm_conv1d.data != NULL);
            if (cfg->layer_is_deltanet[i]) n_dn++; else n_attn++;
        }
        printf("[qmodel] Hybrid layers: %d DeltaNet, %d full-attention\n", n_dn, n_attn);
    }

    /* Tensor type summary — detect any unsupported quant types */
    printf("[qmodel] Tensor types (layer 0):\n");
    {
        qlayer_t *L0 = &qm->layers[0];
        printf("  wq=%s wk=%s wv=%s wo=%s\n",
               dequant_type_name((ggml_type_t)L0->wq.type),
               dequant_type_name((ggml_type_t)L0->wk.type),
               dequant_type_name((ggml_type_t)L0->wv.type),
               dequant_type_name((ggml_type_t)L0->wo.type));
        printf("  gate=%s up=%s down=%s norm=%s\n",
               dequant_type_name((ggml_type_t)L0->w_gate.type),
               dequant_type_name((ggml_type_t)L0->w_up.type),
               dequant_type_name((ggml_type_t)L0->w_down.type),
               dequant_type_name((ggml_type_t)L0->attn_norm.type));
        if (L0->q_norm.data)
            printf("  q_norm=%s k_norm=%s\n",
                   dequant_type_name((ggml_type_t)L0->q_norm.type),
                   dequant_type_name((ggml_type_t)L0->k_norm.type));
        if (L0->post_attn_norm.data)
            printf("  post_attn=%s post_ffw=%s scale=%s\n",
                   dequant_type_name((ggml_type_t)L0->post_attn_norm.type),
                   dequant_type_name((ggml_type_t)L0->post_ffw_norm.type),
                   dequant_type_name((ggml_type_t)L0->layer_output_scale.type));
    }
    printf("  embed=%s output=%s\n",
           dequant_type_name((ggml_type_t)qm->token_embed.type),
           dequant_type_name((ggml_type_t)qm->output_weight.type));

    /* === AUDIT DIAGNOSTICS ===
     * Verify research checklist items against actual loaded tensors. */
    {
        printf("\n=== AUDIT ===\n");

        /* Item 2: PLE gate direction (inp_gate tensor shape) */
        if (qm->layers[0].ple_gate.data) {
            uint64_t n = qm->layers[0].ple_gate.n_elements;
            printf("[AUDIT] inp_gate.n_elements=%llu (expected dim*ple_dim=%d)\n",
                   (unsigned long long)n, qm->config.dim * qm->config.ple_dim);
            printf("[AUDIT] inp_gate.type=%s (F32 expected)\n",
                   dequant_type_name((ggml_type_t)qm->layers[0].ple_gate.type));
        }

        /* Item 3: PLE context-aware component (global per_layer_model_proj) */
        if (qm->ple_proj.data) {
            printf("[AUDIT] per_layer_model_proj LOADED: n_elements=%llu type=%s\n",
                   (unsigned long long)qm->ple_proj.n_elements,
                   dequant_type_name((ggml_type_t)qm->ple_proj.type));
            printf("[AUDIT]   used as context component: BF16 matmul, combine "
                   "(identity + context) / sqrt(2)\n");
        } else {
            printf("[AUDIT] per_layer_model_proj NOT loaded\n");
        }

        /* Item 5: RoPE style */
        printf("[AUDIT] rope_neox=%d (Gemma should be 1)\n", qm->config.rope_neox);

        /* Item 6: query_pre_attn_scalar — Gemma 4 uses 1.0 per llama.cpp
         * f_attn_scale; Q/K RMSNorm handles magnitude, no extra sqrt scaling. */
        printf("[AUDIT] attn_scale=1.0 for Gemma 4 (Q/K RMSNorm in use); "
               "Llama uses 1/sqrt(%d)\n",
               qm->config.head_dim_swa > 0 ? qm->config.head_dim_swa
                                           : qm->config.head_dim);

        /* Norm epsilon */
        printf("[AUDIT] rms_norm_eps=%g (research says 1e-6)\n", qm->config.rms_norm_eps);

        /* Shared KV map — verify sharing is NOT active (all layers should have own KV) */
        if (qm->config.kv_share_map) {
            int n_shared = 0, n_own = 0;
            for (int i = 0; i < qm->config.n_layers; i++) {
                if (qm->config.kv_share_map[i] >= 0) n_shared++;
                else n_own++;
            }
            printf("[AUDIT] kv_share_map: %d own, %d shared (config says shared_kv=%d)\n",
                   n_own, n_shared, qm->config.shared_kv_layers);
        } else {
            printf("[AUDIT] kv_share_map is NULL (all layers compute own KV)\n");
        }

        /* Q/K norm sizes — must match per-layer head_dim */
        if (qm->layers[0].q_norm.data)
            printf("[AUDIT] L0 q_norm.n_elements=%llu (SWA head_dim=%d)\n",
                   (unsigned long long)qm->layers[0].q_norm.n_elements,
                   qm->config.head_dim_swa);
        if (qm->config.n_layers >= 35 && qm->layers[34].q_norm.data)
            printf("[AUDIT] L34 q_norm.n_elements=%llu (global head_dim=%d)\n",
                   (unsigned long long)qm->layers[34].q_norm.n_elements,
                   qm->config.head_dim);

        /* Layer output scale values */
        if (qm->layers[0].layer_output_scale.data &&
            qm->layers[0].layer_output_scale.type == GGML_TYPE_F32) {
            float s0 = *(const float *)qm->layers[0].layer_output_scale.data;
            float s34 = 0.0f;
            if (qm->layers[qm->config.n_layers - 1].layer_output_scale.data)
                s34 = *(const float *)qm->layers[qm->config.n_layers - 1].layer_output_scale.data;
            printf("[AUDIT] layer_output_scale L0=%.4f L%d=%.4f\n",
                   s0, qm->config.n_layers - 1, s34);
        }

        printf("=== END AUDIT ===\n\n");
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

    /* RoPE lookup tables (fills once per state) */
    rope_ensure_tables(state, c, qm->rope_freqs);

    /* Bounds check token ID.
     * For invalid tokens: use zero embedding but still run the full forward pass.
     * This is critical for SSM/DeltaNet models — the recurrence S = gate*S + k⊗delta
     * MUST execute at every position to maintain the temporal chain. Skipping a position
     * would break conv1d sliding window and SSM state decay. With zero embedding,
     * the recurrence becomes S = gate*S + 0 (proper decay, no new information). */
    if (token < 0 || token >= c->vocab_size) {
        memset(state->x, 0, (size_t)dim * sizeof(float));
    } else {
        /* 1. EMBEDDING: dequantize one row of token_embed into x */
        qembed_lookup(&qm->token_embed, token, state->x, dim);
    }

    /* Gemma embedding scaling: multiply by sqrt(dim) after lookup */
    if (c->embed_scale) {
        float scale = sqrtf((float)dim);
        for (int i = 0; i < dim; i++)
            state->x[i] *= scale;
    }

    /* PLE: compute per-layer embedding slices for this token.
     * per_layer_token_embd has shape (n_layers * ple_dim, vocab_size) — each token
     * has n_layers * ple_dim values (e.g. 35 * 256 = 8960 for Gemma 4 E2B).
     * Each layer's slice is ONE Q5_K block of 256 values.
     *
     * Byte offset for token T, layer L:
     *   (T * n_layers + L) * block_bytes
     *
     * After lookup, apply per_layer_proj_norm (shared across layers) and
     * scale by sqrt(ple_dim) per the Gemma 4 PLE recipe.
     */
    int has_ple = (!G4_DISABLE_PLE && c->ple_dim > 0 && qm->ple_embed.data != NULL
                    && state->ple_all != NULL);
    /* Use heap-allocated state buffers (too big for seL4 stack: 35*256*4 = 35KB each) */
    float *ple_all = state->ple_all;
    if (has_ple) {
        ggml_type_t ple_type = (ggml_type_t)qm->ple_embed.type;
        int block_size = dequant_type_block_size(ple_type);
        size_t block_bytes = dequant_type_block_bytes(ple_type);

        if (block_size == c->ple_dim && block_bytes > 0) {
            /* === Token-identity component ===
             * Look up per-layer slice from per_layer_token_embd and scale by sqrt(ple_dim).
             * Research says: NO RMSNorm on token-identity component. */
            const uint8_t *base = (const uint8_t *)qm->ple_embed.data;
            for (int L = 0; L < c->n_layers; L++) {
                size_t off = ((size_t)token * c->n_layers + L) * block_bytes;
                dequant_row(base + off, &ple_all[L * c->ple_dim],
                            c->ple_dim, ple_type);
            }
            float ple_scale = sqrtf((float)c->ple_dim);
            for (int i = 0; i < c->n_layers * c->ple_dim; i++)
                ple_all[i] *= ple_scale;

            /* === Context-aware component ===
             * Project current embedding (state->x, after embed_scale) through
             * per_layer_model_proj (1536 -> 8960), scale by 1/sqrt(dim),
             * reshape to [n_layers, ple_dim], apply per_layer_proj_norm.
             * Then combine: ple_all[L] = (identity[L] + context[L]) / sqrt(2)
             */
            if (qm->ple_proj.data && qm->ple_proj.n_elements ==
                (uint64_t)dim * c->n_layers * c->ple_dim && state->ple_context) {
                /* Heap-allocated context buffer (was stack: 35KB crashed seL4) */
                float *context = state->ple_context;
                int ctx_out_dim = c->n_layers * c->ple_dim;
                ggml_type_t ptype = (ggml_type_t)qm->ple_proj.type;

                if (ptype == GGML_TYPE_F32) {
                    const float *W = (const float *)qm->ple_proj.data;
                    for (int m = 0; m < ctx_out_dim; m++) {
                        const float *row = W + (size_t)m * dim;
                        float dot = 0.0f;
                        for (int k = 0; k < dim; k++)
                            dot += row[k] * state->x[k];
                        context[m] = dot;
                    }
                } else if (ptype == GGML_TYPE_BF16) {
                    const uint16_t *W = (const uint16_t *)qm->ple_proj.data;
                    for (int m = 0; m < ctx_out_dim; m++) {
                        const uint16_t *row = W + (size_t)m * dim;
                        float dot = 0.0f;
                        for (int k = 0; k < dim; k++) {
                            union { uint32_t u; float f; } un;
                            un.u = ((uint32_t)row[k]) << 16;
                            dot += un.f * state->x[k];
                        }
                        context[m] = dot;
                    }
                } else {
                    /* Unsupported type — zero out context (fall back to identity-only) */
                    memset(context, 0, (size_t)ctx_out_dim * sizeof(float));
                }

                /* Scale context by 1/sqrt(dim) */
                float ctx_scale = 1.0f / sqrtf((float)dim);
                for (int i = 0; i < ctx_out_dim; i++)
                    context[i] *= ctx_scale;

                /* Apply per_layer_proj_norm (256-dim RMSNorm) to each layer's slice */
                float norm_eps_ple = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
                if (qm->ple_norm.data && qm->ple_norm.type == GGML_TYPE_F32) {
                    const float *norm_w = (const float *)qm->ple_norm.data;
                    for (int L = 0; L < c->n_layers; L++) {
                        float *slice = &context[L * c->ple_dim];
                        tensor_rms_norm(slice, norm_w, slice, c->ple_dim, norm_eps_ple);
                    }
                }

                /* Combine: ple_all[i] = (ple_all[i] + context[i]) / sqrt(2) */
                float combine_scale = 1.0f / sqrtf(2.0f);
                for (int i = 0; i < ctx_out_dim; i++)
                    ple_all[i] = (ple_all[i] + context[i]) * combine_scale;
            }
        } else {
            has_ple = 0;  /* disable if block layout doesn't match */
        }
    }

    /* 2. TRANSFORMER LAYERS */
    for (int L = 0; L < c->n_layers; L++) {
        const qlayer_t *layer = &qm->layers[L];

        /* --- Attention --- */

        /* Per-layer attention dimensions:
         * SWA layers use head_dim_swa; global layers use head_dim.
         * For Llama: layer_is_swa is NULL, is_swa is always 0 -> all layers use head_dim. */
        int is_swa = (c->layer_is_swa && c->layer_is_swa[L]);
        int is_deltanet = (c->layer_is_deltanet && c->layer_is_deltanet[L]);
        int l_head_dim = (is_swa && c->head_dim_swa > 0) ? c->head_dim_swa : c->head_dim;
        int l_kv_dim   = n_kv_heads * l_head_dim;
        int l_q_dim    = n_heads * l_head_dim;

        /* Gated Q detection (Qwen3.5 full-attention layers):
         * wq has 2x the expected rows = n_heads * head_dim * 2 (interleaved Q + gate).
         * For Llama/Gemma: wq.n_elements == l_q_dim * dim, has_gated_q is false. */
        int has_gated_q = (!is_deltanet && layer->wq.data &&
                           layer->wq.n_elements == (uint64_t)(l_q_dim * 2) * (uint64_t)dim);

        float layer_norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;

        if (is_deltanet && layer->ssm_conv1d.data && state->qkv_scratch) {
            /* ============ DELTANET RECURRENT LAYER ============
             * Gated DeltaNet: conv1d → L2 norm → discretize → delta-rule recurrence → output gate
             * Uses SSM state (fixed size) instead of KV cache (growing). */
/* DeltaNet per-layer debug tracing. Default OFF for clean output.
 * Re-enable with -DDELTANET_DEBUG=1 on compiler command line. */
#ifndef DELTANET_DEBUG
#define DELTANET_DEBUG 0
#endif

            int num_k_heads = c->ssm_n_group;
            int num_v_heads = c->ssm_dt_rank;
            int head_k_dim_ssm = c->ssm_d_state;
            int head_v_dim_ssm = c->ssm_d_inner / num_v_heads;
            int q_dim = num_k_heads * head_k_dim_ssm;    /* 2048 */
            int k_dim = q_dim;                             /* 2048 */
            int v_dim = c->ssm_d_inner;                   /* 4096 */
            int qkv_channels = q_dim + k_dim + v_dim;     /* 8192 */

            /* a. RMS norm input */
            if (layer->attn_norm.type == GGML_TYPE_F32) {
                const float *norm = (const float *)layer->attn_norm.data;
                tensor_rms_norm(state->x, norm, state->xb, dim, layer_norm_eps);
            } else {
                dequant_row(layer->attn_norm.data, state->xb2, dim,
                            (ggml_type_t)layer->attn_norm.type);
                tensor_rms_norm(state->x, state->xb2, state->xb, dim, layer_norm_eps);
            }

            /* b. QKV projection: attn_qkv @ xb → [qkv_channels] in scratch */
            float *qkv = state->qkv_scratch;
            qmatmul_vec(&layer->wq, state->xb, qkv, q_dim, dim);       /* Q */
            qmatmul_vec(&layer->wk, state->xb, qkv + q_dim, k_dim, dim); /* K */
            qmatmul_vec(&layer->wv, state->xb, qkv + q_dim + k_dim, v_dim, dim); /* V */

#if DELTANET_DEBUG
            if (L == 0 && state->pos == 0) {
                printf("[DN L0@0] pre-conv Q[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[0], qkv[1], qkv[2], qkv[3]);
                printf("[DN L0@0] pre-conv K[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[q_dim], qkv[q_dim+1], qkv[q_dim+2], qkv[q_dim+3]);
                printf("[DN L0@0] pre-conv V[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[q_dim+k_dim], qkv[q_dim+k_dim+1], qkv[q_dim+k_dim+2], qkv[q_dim+k_dim+3]);
                float qnorm = 0;
                for (int i = 0; i < qkv_channels; i++) qnorm += qkv[i] * qkv[i];
                printf("[DN L0@0] pre-conv |QKV|=%.2f\n", sqrtf(qnorm));
            }
#endif

            /* c. Z output gate: attn_out_gate @ xb → [v_dim] */
            float *z_buf = qkv + qkv_channels;  /* after QKV in scratch */
            qmatmul_vec(&layer->attn_out_gate, state->xb, z_buf, v_dim, dim);

            /* d. Alpha/beta projections */
            float alpha_raw[64], beta_raw[64]; /* max num_v_heads */
            qmatmul_vec(&layer->ssm_alpha, state->xb, alpha_raw, num_v_heads, dim);
            qmatmul_vec(&layer->ssm_beta, state->xb, beta_raw, num_v_heads, dim);

#if DELTANET_DEBUG
            if (L == 0 && state->pos == 0) {
                printf("[DN L0@0] alpha[0..3]: %.4f %.4f %.4f %.4f\n",
                       alpha_raw[0], alpha_raw[1], alpha_raw[2], alpha_raw[3]);
                printf("[DN L0@0] beta[0..3]: %.4f %.4f %.4f %.4f\n",
                       beta_raw[0], beta_raw[1], beta_raw[2], beta_raw[3]);
                printf("[DN L0@0] z[0..3]: %.4f %.4f %.4f %.4f\n",
                       z_buf[0], z_buf[1], z_buf[2], z_buf[3]);
            }
#endif

            /* e. Conv1d with state */
            /* Find this layer's conv state index (count DeltaNet layers before L) */
            int dn_idx = 0;
            for (int j = 0; j < L; j++)
                if (c->layer_is_deltanet[j]) dn_idx++;
            float *cs = state->conv_state +
                        (size_t)dn_idx * (c->ssm_d_conv - 1) * qkv_channels;

            /* Dequant conv kernel to F32 if needed */
            const float *conv_k;
            float *conv_k_buf = state->fwd_scratch; /* max 4*8192 = 32768 floats (was 128KB stack) */
            if (layer->ssm_conv1d.type == GGML_TYPE_F32) {
                conv_k = (const float *)layer->ssm_conv1d.data;
            } else {
                int conv_elems = c->ssm_d_conv * qkv_channels;
                dequant_row(layer->ssm_conv1d.data, conv_k_buf, conv_elems,
                            (ggml_type_t)layer->ssm_conv1d.type);
                conv_k = conv_k_buf;
            }

            deltanet_conv1d(qkv, cs, conv_k, c->ssm_d_conv, qkv_channels);

#if DELTANET_DEBUG
            if (L == 0 && state->pos == 0) {
                printf("[DN L0@0] post-conv Q[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[0], qkv[1], qkv[2], qkv[3]);
                printf("[DN L0@0] post-conv K[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[q_dim], qkv[q_dim+1], qkv[q_dim+2], qkv[q_dim+3]);
                printf("[DN L0@0] post-conv V[0..3]: %.4f %.4f %.4f %.4f\n",
                       qkv[q_dim+k_dim], qkv[q_dim+k_dim+1], qkv[q_dim+k_dim+2], qkv[q_dim+k_dim+3]);
                printf("[DN L0@0] conv_k[0..3]: %.6f %.6f %.6f %.6f\n",
                       conv_k[0], conv_k[1], conv_k[2], conv_k[3]);
            }
#endif

            /* f. Dequant ssm_a and dt_bias */
            float a_buf[64], dt_buf[64]; /* max num_v_heads */
            if (layer->ssm_a.type == GGML_TYPE_F32)
                memcpy(a_buf, layer->ssm_a.data, (size_t)num_v_heads * sizeof(float));
            else
                dequant_row(layer->ssm_a.data, a_buf, num_v_heads,
                            (ggml_type_t)layer->ssm_a.type);
            if (layer->ssm_dt_bias.type == GGML_TYPE_F32)
                memcpy(dt_buf, layer->ssm_dt_bias.data, (size_t)num_v_heads * sizeof(float));
            else
                dequant_row(layer->ssm_dt_bias.data, dt_buf, num_v_heads,
                            (ggml_type_t)layer->ssm_dt_bias.type);

            /* g. Dequant ssm_norm */
            float norm_buf_ssm[256]; /* max head_v_dim */
            if (layer->ssm_norm.type == GGML_TYPE_F32)
                memcpy(norm_buf_ssm, layer->ssm_norm.data, (size_t)head_v_dim_ssm * sizeof(float));
            else
                dequant_row(layer->ssm_norm.data, norm_buf_ssm, head_v_dim_ssm,
                            (ggml_type_t)layer->ssm_norm.type);

            /* h. DeltaNet recurrence: conv1d output → delta rule → gated output */
            float *ssm_s = state->ssm_state +
                           (size_t)dn_idx * num_v_heads * head_k_dim_ssm * head_v_dim_ssm;
            float *dn_out = state->fwd_scratch; /* max v_dim (was 16KB stack) */

            deltanet_forward(qkv, qkv + q_dim, qkv + q_dim + k_dim,
                             z_buf, alpha_raw, beta_raw,
                             a_buf, dt_buf, norm_buf_ssm, layer_norm_eps,
                             ssm_s, dn_out,
                             num_k_heads, num_v_heads,
                             head_k_dim_ssm, head_v_dim_ssm);


#if DELTANET_DEBUG
            if (L == 0) {
                float onorm = 0, snorm = 0;
                for (int i = 0; i < v_dim; i++) onorm += dn_out[i] * dn_out[i];
                int state_size = num_v_heads * head_k_dim_ssm * head_v_dim_ssm;
                for (int i = 0; i < state_size; i++) snorm += ssm_s[i] * ssm_s[i];
                printf("[DN L0@%d] |out|=%.3f |S|=%.3f out[0..2]=%.4f %.4f %.4f\n",
                       state->pos, sqrtf(onorm), sqrtf(snorm),
                       dn_out[0], dn_out[1], dn_out[2]);
            }
#endif

            /* i. Output projection: ssm_out @ dn_out → xb2 [dim] */
            qmatmul_vec(&layer->ssm_out, dn_out, state->xb2, dim, v_dim);

            /* j. Post-attention norm (sandwich) */
            if (layer->post_attn_norm.data) {
                if (layer->post_attn_norm.type == GGML_TYPE_F32) {
                    const float *norm = (const float *)layer->post_attn_norm.data;
                    tensor_rms_norm(state->xb2, norm, state->xb2, dim, layer_norm_eps);
                } else {
                    float *nbuf = state->fwd_scratch; /* max dim (was 16KB stack) */
                    dequant_row(layer->post_attn_norm.data, nbuf, dim,
                                (ggml_type_t)layer->post_attn_norm.type);
                    tensor_rms_norm(state->xb2, nbuf, state->xb2, dim, layer_norm_eps);
                }
            }

            /* k. Residual */
            residual_add(state->x, state->xb2, dim);

#if DELTANET_DEBUG
            if (state->pos == 0) {
                float xn = 0;
                for (int i = 0; i < dim; i++) xn += state->x[i] * state->x[i];
                if (L < 4 || L == 3 || L == 7 || L == 31)
                    printf("[L%d@0] post-layer |x|=%.2f x[0..2]=%.4f %.4f %.4f %s\n",
                           L, sqrtf(xn), state->x[0], state->x[1], state->x[2],
                           is_deltanet ? "DN" : "ATT");
            }
#endif

        } else if (is_deltanet) {
            /* DeltaNet layer but missing tensors — identity pass-through */
        } else {
        /* ============ ATTENTION BLOCK (full-attention or standard GQA) ============ */

        /* a. RMS norm -> xb */
        if (layer->attn_norm.type == GGML_TYPE_F32) {
            const float *norm = (const float *)layer->attn_norm.data;
            tensor_rms_norm(state->x, norm, state->xb, dim, layer_norm_eps);
        } else {
            dequant_row(layer->attn_norm.data, state->xb2, dim,
                        (ggml_type_t)layer->attn_norm.type);
            tensor_rms_norm(state->x, state->xb2, state->xb, dim, layer_norm_eps);
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

        if (has_own_kv) {
            /* b-d. Normal: Q/K/V projections with PER-LAYER output dims.
             *    Q: [l_q_dim x dim],  K/V: [l_kv_dim x dim]
             *    For Llama: l_q_dim == dim, l_kv_dim == kv_dim (unchanged).
             *    Gated Q (Qwen3.5 full-attn): wq is [l_q_dim*2 x dim], interleaved
             *    [Q_h0, Gate_h0, Q_h1, Gate_h1, ...]. Deinterleave into q + scratch. */
            if (has_gated_q && state->qkv_scratch) {
                /* Project into scratch: [l_q_dim * 2] output */
                qmatmul_vec(&layer->wq, state->xb, state->qkv_scratch, l_q_dim * 2, dim);
                /* Deinterleave: Q → state->q, Gate → scratch[l_q_dim*2 .. l_q_dim*3-1] */
                float *gate_buf = state->qkv_scratch + l_q_dim * 2;
                for (int h = 0; h < n_heads; h++) {
                    memcpy(state->q + h * l_head_dim,
                           state->qkv_scratch + h * 2 * l_head_dim,
                           (size_t)l_head_dim * sizeof(float));
                    memcpy(gate_buf + h * l_head_dim,
                           state->qkv_scratch + h * 2 * l_head_dim + l_head_dim,
                           (size_t)l_head_dim * sizeof(float));
                }
            } else {
                qmatmul_vec(&layer->wq, state->xb, state->q,  l_q_dim,  dim);
            }
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

            /* V raw RMSNorm (Gemma 4): normalize V per-head WITHOUT learned weights.
             * Each head's V is divided by its RMS; no weight multiply. */
            if (!G4_DISABLE_QK_NORM && c->head_dim_swa > 0) {  /* Gemma 4 only */
                for (int h = 0; h < n_kv_heads; h++) {
                    float *v_head = state->v + h * l_head_dim;
                    float sum_sq = 0;
                    for (int d = 0; d < l_head_dim; d++)
                        sum_sq += v_head[d] * v_head[d];
                    float rms = sqrtf(sum_sq / l_head_dim + norm_eps);
                    for (int d = 0; d < l_head_dim; d++)
                        v_head[d] /= rms;
                }
            }

            /* e. RoPE on Q AND K — uses l_head_dim (256 for SWA, 512 for global).
             * Dual theta: SWA layers use rope_theta_swa, global use rope_theta.
             * NEOX (Gemma): pair d[i] with d[i + half] (split first/second half).
             * Standard (Llama): pair d[2i] with d[2i+1] (interleaved).
             * Partial RoPE (Qwen3.5): only first rope_dim_count dims get rotation. */
            {
                int rope_dim = l_head_dim;
                if (c->rope_dim_count > 0 && c->rope_dim_count < rope_dim)
                    rope_dim = c->rope_dim_count;
                int half = rope_dim / 2;
                for (int h = 0; h < n_heads; h++) {
                    for (int i = 0; i < half; i++) {
                        const float *cos_tbl = is_swa ? state->rope_cos_swa : state->rope_cos;
                        const float *sin_tbl = is_swa ? state->rope_sin_swa : state->rope_sin;
                        float cos_a = cos_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        float sin_a = sin_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        int idx0, idx1;
                        if (c->rope_neox) {
                            idx0 = h * l_head_dim + i;          /* first half */
                            idx1 = h * l_head_dim + i + half;   /* second half */
                        } else {
                            idx0 = h * l_head_dim + 2 * i;      /* interleaved */
                            idx1 = h * l_head_dim + 2 * i + 1;
                        }
                        float r0 = state->q[idx0], r1 = state->q[idx1];
                        state->q[idx0] = r0 * cos_a - r1 * sin_a;
                        state->q[idx1] = r0 * sin_a + r1 * cos_a;
                    }
                }
                for (int h = 0; h < n_kv_heads; h++) {
                    for (int i = 0; i < half; i++) {
                        const float *cos_tbl = is_swa ? state->rope_cos_swa : state->rope_cos;
                        const float *sin_tbl = is_swa ? state->rope_sin_swa : state->rope_sin;
                        float cos_a = cos_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        float sin_a = sin_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        int idx0, idx1;
                        if (c->rope_neox) {
                            idx0 = h * l_head_dim + i;
                            idx1 = h * l_head_dim + i + half;
                        } else {
                            idx0 = h * l_head_dim + 2 * i;
                            idx1 = h * l_head_dim + 2 * i + 1;
                        }
                        float r0 = state->k[idx0], r1 = state->k[idx1];
                        state->k[idx0] = r0 * cos_a - r1 * sin_a;
                        state->k[idx1] = r0 * sin_a + r1 * cos_a;
                    }
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
            {
                int rope_dim_q = l_head_dim;
                if (c->rope_dim_count > 0 && c->rope_dim_count < rope_dim_q)
                    rope_dim_q = c->rope_dim_count;
                int half = rope_dim_q / 2;
                for (int h = 0; h < n_heads; h++) {
                    for (int i = 0; i < half; i++) {
                        const float *cos_tbl = is_swa ? state->rope_cos_swa : state->rope_cos;
                        const float *sin_tbl = is_swa ? state->rope_sin_swa : state->rope_sin;
                        float cos_a = cos_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        float sin_a = sin_tbl[(size_t)pos * (size_t)state->rope_table_half + (size_t)i];
                        int idx0, idx1;
                        if (c->rope_neox) {
                            idx0 = h * l_head_dim + i;
                            idx1 = h * l_head_dim + i + half;
                        } else {
                            idx0 = h * l_head_dim + 2 * i;
                            idx1 = h * l_head_dim + 2 * i + 1;
                        }
                        float r0 = state->q[idx0], r1 = state->q[idx1];
                        state->q[idx0] = r0 * cos_a - r1 * sin_a;
                        state->q[idx1] = r0 * sin_a + r1 * cos_a;
                    }
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

            /* Score: Q . K^T * attn_scale.
             * Gemma 4: f_attention_scale = 1.0 (per llama.cpp).
             * Q/K RMSNorm handles magnitude control — no additional sqrt scaling.
             * For Llama: use standard 1/sqrt(head_dim). */
            float attn_scale = (c->head_dim_swa > 0)
                ? 1.0f  /* Gemma 4: no sqrt scaling */
                : 1.0f / sqrtf((float)l_head_dim);  /* Llama: standard */
            for (int t = att_start; t <= pos; t++) {
                float *k_t = key_layer + t * max_kv_dim + kv_h * l_head_dim;
                float score;
#ifdef __AVX2__
                score = dot_f32_avx2(q_head, k_t, l_head_dim);
#else
                score = 0.0f;
                for (int d = 0; d < l_head_dim; d++)
                    score += q_head[d] * k_t[d];
#endif
                att_h[t] = score * attn_scale;
            }

            /* Softmax over [att_start..pos] */
            tensor_softmax(att_h + att_start, att_h + att_start, pos + 1 - att_start);

            /* Weighted sum of V -> xb (using l_head_dim per head) */
            float *out_h = state->xb + h * l_head_dim;
            memset(out_h, 0, (size_t)l_head_dim * sizeof(float));
            for (int t = att_start; t <= pos; t++) {
                float *v_t = val_layer + t * max_kv_dim + kv_h * l_head_dim;
                float w = att_h[t];
#ifdef __AVX2__
                accum_axpy_f32_avx2(out_h, v_t, w, l_head_dim);
#else
                for (int d = 0; d < l_head_dim; d++)
                    out_h[d] += w * v_t[d];
#endif
            }
        }

        /* Gated Q: apply sigmoid(gate) to attention output BEFORE projection.
         * Gate lives in attention-output space [n_heads * head_dim = 4096],
         * NOT in projected space [dim = 2560]. Must apply before wo projection. */
        if (has_gated_q && state->qkv_scratch) {
            float *gate_buf = state->qkv_scratch + l_q_dim * 2;
            for (int i = 0; i < l_q_dim; i++)
                state->xb[i] *= 1.0f / (1.0f + expf(-gate_buf[i]));
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
                float *norm_buf = state->fwd_scratch; /* max dim (was 16KB stack) */
                dequant_row(layer->post_attn_norm.data, norm_buf, dim,
                            (ggml_type_t)layer->post_attn_norm.type);
                tensor_rms_norm(state->xb2, norm_buf, state->xb2, dim, norm_eps);
            }
        }

        /* i. Residual connection */
        residual_add(state->x, state->xb2, dim);

        } /* end if (!is_deltanet) — attention block */

        /* --- Feed-Forward Network (SwiGLU) --- */

        /* j. RMS norm -> xb (use config eps) */
        if (layer->ffn_norm.type == GGML_TYPE_F32) {
            const float *norm = (const float *)layer->ffn_norm.data;
            tensor_rms_norm(state->x, norm, state->xb, dim, layer_norm_eps);
        } else {
            dequant_row(layer->ffn_norm.data, state->xb2, dim,
                        (ggml_type_t)layer->ffn_norm.type);
            tensor_rms_norm(state->x, state->xb2, state->xb, dim, layer_norm_eps);
        }

        /* k-m. SwiGLU: silu(gate(xb)) * up(xb)
         *    Per-layer FFN hidden dim for variable-FFN models (Gemma 4).
         *    For Llama: layer_ffn_dim is NULL -> l_ffn_dim == hidden_dim (unchanged). */
        int l_ffn_dim = (c->layer_ffn_dim) ? c->layer_ffn_dim[L] : hidden_dim;
        qmatmul_vec(&layer->w_gate, state->xb, state->hb,  l_ffn_dim, dim);
        qmatmul_vec(&layer->w_up,   state->xb, state->hb2, l_ffn_dim, dim);
        if (c->use_gelu)
            tensor_gelu(state->hb, state->hb, l_ffn_dim);  /* GeGLU (Gemma) */
        else
            tensor_silu(state->hb, state->hb, l_ffn_dim);  /* SwiGLU (Llama) */
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
                float *norm_buf = state->fwd_scratch; /* max dim (was 16KB stack) */
                dequant_row(layer->post_ffw_norm.data, norm_buf, dim,
                            (ggml_type_t)layer->post_ffw_norm.type);
                tensor_rms_norm(state->xb, norm_buf, state->xb, dim, norm_eps);
            }
        }

        /* o. Residual connection */
        residual_add(state->x, state->xb, dim);

#if DELTANET_DEBUG
        if (state->pos == 0 && (L < 4 || L == 7 || L == 31)) {
            float xn = 0;
            for (int i = 0; i < dim; i++) xn += state->x[i] * state->x[i];
            printf("[L%d@0] post-FFN |x|=%.2f x[0..2]=%.4f %.4f %.4f %s\n",
                   L, sqrtf(xn), state->x[0], state->x[1], state->x[2],
                   is_deltanet ? "DN" : "ATT");
        }
#endif

        /* --- PLE: Per-Layer Embedding injection (Gemma 4) ---
         * Applied AFTER the attention+FFN sandwich, BEFORE layer_output_scale.
         * Formula:
         *   gate  = GELU(inp_gate @ x)          # 1536 -> 256
         *   mixed = gate * ple_slice[L]         # elementwise 256
         *   proj  = ple_proj @ mixed            # 256 -> 1536
         *   proj  = RMSNorm(proj, post_norm)    # per-layer 1536-dim norm
         *   x    += proj                        # residual
         */
        if (has_ple && layer->ple_gate.data && layer->ple_proj.data) {
            const float *ple_slice = &ple_all[L * c->ple_dim];

            /* gate = inp_gate @ x (1536 -> 256) */
            float gate[256];
            qmatmul_vec(&layer->ple_gate, state->x, gate, c->ple_dim, dim);

            /* GELU and multiply with ple_slice */
            for (int d = 0; d < c->ple_dim; d++) {
                float g = gate[d];
                float g3 = g * g * g;
                g = 0.5f * g * (1.0f + tanhf(0.7978845608f * (g + 0.044715f * g3)));
                gate[d] = g * ple_slice[d];
            }

            /* proj = ple_proj @ gate (256 -> 1536) */
            float *ple_out = state->fwd_scratch; /* max dim (was 16KB stack) */
            qmatmul_vec(&layer->ple_proj, gate, ple_out, dim, c->ple_dim);

            /* Apply per-layer post_norm (if present) */
            if (layer->post_norm.data && layer->post_norm.type == GGML_TYPE_F32) {
                float norm_eps_post = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
                const float *pn = (const float *)layer->post_norm.data;
                tensor_rms_norm(ple_out, pn, ple_out, dim, norm_eps_post);
            }

            /* Residual add */
            for (int d = 0; d < dim; d++)
                state->x[d] += ple_out[d];
        }

#if JARVIS_DBG_FORWARD
        /* DEBUG: per-layer x norm trace (first 2 tokens, key layers) */
        if (state->pos < 2 && (L < 3 || L == 17 || L == 34)) {
            float norm2 = 0;
            for (int d = 0; d < dim; d++) norm2 += state->x[d] * state->x[d];
            printf("[L%02d@%d] |x|=%.1f x[0..2]=%.4f %.4f %.4f\n",
                   L, state->pos, sqrtf(norm2),
                   state->x[0], state->x[1], state->x[2]);
        }
#endif

        /* Layer output scaling (Gemma 4) — scalar broadcast.
         * layer_output_scale is a single float {1}, NOT a [dim] vector.
         * Multiply all elements of x by this one value. */
        if (!G4_DISABLE_SCALE && layer->layer_output_scale.data) {
            float s;
            if (layer->layer_output_scale.type == GGML_TYPE_F32) {
                s = *(const float *)layer->layer_output_scale.data;
            } else {
                float tmp;
                dequant_row(layer->layer_output_scale.data, &tmp, 1,
                            (ggml_type_t)layer->layer_output_scale.type);
                s = tmp;
            }
            tensor_scale(state->x, s, state->x, dim);
        }
    }

    /* 3. Final RMS norm (in-place via xb then copy back) — use config eps */
    {
        float final_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
        if (qm->output_norm.type == GGML_TYPE_F32) {
            const float *norm = (const float *)qm->output_norm.data;
            tensor_rms_norm(state->x, norm, state->xb, dim, final_eps);
        } else {
            dequant_row(qm->output_norm.data, state->xb2, dim,
                        (ggml_type_t)qm->output_norm.type);
            tensor_rms_norm(state->x, state->xb2, state->xb, dim, final_eps);
        }
    }
    memcpy(state->x, state->xb, (size_t)dim * sizeof(float));

    /* 4. Output projection -> logits (quantized matmul) */
#if DELTANET_DEBUG
    if (state->pos < 2) {
        float xn = 0;
        for (int i = 0; i < dim; i++) xn += state->x[i] * state->x[i];
        printf("[FWD@%d] pre-output |x|=%.4f x[0..2]=%.4f %.4f %.4f\n",
               state->pos, sqrtf(xn), state->x[0], state->x[1], state->x[2]);
        printf("[FWD@%d] output_weight: data=%p type=%u n_elem=%llu\n",
               state->pos, (void*)qm->output_weight.data, qm->output_weight.type,
               (unsigned long long)qm->output_weight.n_elements);
    }
#endif
    qmatmul_vec(&qm->output_weight, state->x, state->logits,
                c->vocab_size, dim);
#if DELTANET_DEBUG
    printf("[FWD@%d] logit0=%.4f |x|pre=%.2f\n", state->pos, state->logits[0],
           sqrtf(state->x[0]*state->x[0]+state->x[1]*state->x[1]+state->x[2]*state->x[2]));
    if (state->pos < 2) {
        printf("[FWD@%d] logits[0..4]: %.4f %.4f %.4f %.4f %.4f\n",
               state->pos, state->logits[0], state->logits[1],
               state->logits[2], state->logits[3], state->logits[4]);
        /* Manual row-0 dot product for sanity check */
        int bs = dequant_type_block_size((ggml_type_t)qm->output_weight.type);
        size_t bb = dequant_type_block_bytes((ggml_type_t)qm->output_weight.type);
        printf("[FWD@%d] output_weight bs=%d bb=%zu type=%d\n",
               state->pos, bs, bb, qm->output_weight.type);
        if (bs > 0 && bb > 0) {
            size_t row_bytes = ((size_t)dim / bs) * bb;
            float tmp[256];
            float dot = 0;
            const uint8_t *row0 = (const uint8_t *)qm->output_weight.data;
            int n_blocks = dim / bs;
            for (int b = 0; b < n_blocks && b < 1; b++) {
                dequant_row(row0 + b * bb, tmp, bs, (ggml_type_t)qm->output_weight.type);
                printf("[FWD@%d] row0 block0[0..3]: %.4f %.4f %.4f %.4f\n",
                       state->pos, tmp[0], tmp[1], tmp[2], tmp[3]);
            }
            (void)row_bytes; (void)dot;
        }
    }
#endif

    /* 4b. Logit softcapping (Gemma 4) */
    if (!G4_DISABLE_SOFTCAP && c->logit_softcap > 0.0f)
        logit_softcap(state->logits, c->vocab_size, c->logit_softcap);

#if JARVIS_DBG_FORWARD
    /* DEBUG: print first-token logits + top-5 after full prompt */
    if (state->pos == 0) {
        printf("[FWD] pos0 logits[0..4]: %.4f %.4f %.4f %.4f %.4f\n",
               state->logits[0], state->logits[1], state->logits[2],
               state->logits[3], state->logits[4]);
    }
    /* Top-5 tokens after processing the last prompt token */
    if (state->pos < 20) {
        int top[5] = {0, 0, 0, 0, 0};
        for (int i = 1; i < c->vocab_size; i++)
            for (int j = 0; j < 5; j++)
                if (state->logits[i] > state->logits[top[j]]) {
                    for (int k = 4; k > j; k--) top[k] = top[k-1];
                    top[j] = i;
                    break;
                }
        printf("[TOP5@%d] ", state->pos);
        for (int j = 0; j < 5; j++)
            printf("%d(%.1f) ", top[j], state->logits[top[j]]);
        printf("\n");
    }
#endif

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

    /* Reset SSM state (conv sliding window + recurrent state matrix) */
    if (state->conv_state && state->n_deltanet > 0) {
        const llama_config_t *c = &qm->config;
        int qkv_dim = c->ssm_n_group * c->ssm_d_state * 2 + c->ssm_d_inner;
        size_t conv_bytes = (size_t)state->n_deltanet * (c->ssm_d_conv - 1) *
                            qkv_dim * sizeof(float);
        memset(state->conv_state, 0, conv_bytes);
    }
    if (state->ssm_state && state->n_deltanet > 0) {
        const llama_config_t *c = &qm->config;
        int head_v_dim = c->ssm_d_inner / c->ssm_dt_rank;
        size_t ssm_bytes = (size_t)state->n_deltanet * c->ssm_dt_rank *
                           c->ssm_d_state * head_v_dim * sizeof(float);
        memset(state->ssm_state, 0, ssm_bytes);
    }

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
