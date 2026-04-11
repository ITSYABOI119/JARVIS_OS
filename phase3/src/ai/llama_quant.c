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

/* Per-forward-pass debug tracing — gates [L00@N], [FWD], [TOP5@N] diagnostics.
 * Default is OFF (silent) for performance; pass -DJARVIS_DBG_FORWARD=1 on the
 * compiler command line to re-enable. Also documented in jarvis_debug.h. */
#ifndef JARVIS_DBG_FORWARD
#define JARVIS_DBG_FORWARD 0
#endif

#define RMS_EPS 1e-5f

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

        /* K/V weights: always optional — shared-KV layers may lack wk/wv tensors.
         * Presence/absence is used below to derive the KV share map from ground truth. */
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
        int l_head_dim = (is_swa && c->head_dim_swa > 0) ? c->head_dim_swa : c->head_dim;
        int l_kv_dim   = n_kv_heads * l_head_dim;
        int l_q_dim    = n_heads * l_head_dim;

        /* a. RMS norm -> xb
         *    Use config's rms_norm_eps (1e-6 for Gemma 4, 1e-5 for Llama).
         *    Norm weights are F32 in the GGUF — cast data pointer directly.
         *    For non-F32 norm weights (unlikely), dequant into xb2 as scratch. */
        float layer_norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
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
             * Standard (Llama): pair d[2i] with d[2i+1] (interleaved). */
            {
                int half = l_head_dim / 2;
                for (int h = 0; h < n_heads; h++) {
                    for (int i = 0; i < half; i++) {
                        float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                        if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                        float angle = (float)pos * freq;
                        float cos_a = cosf(angle);
                        float sin_a = sinf(angle);
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
                        float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                        if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                        float angle = (float)pos * freq;
                        float cos_a = cosf(angle);
                        float sin_a = sinf(angle);
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
                int half = l_head_dim / 2;
                for (int h = 0; h < n_heads; h++) {
                    for (int i = 0; i < half; i++) {
                        float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                        if (qm->rope_freqs && !is_swa) freq /= qm->rope_freqs[i];
                        float angle = (float)pos * freq;
                        float cos_a = cosf(angle);
                        float sin_a = sinf(angle);
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
                float score = 0.0f;
                for (int d = 0; d < l_head_dim; d++)
                    score += q_head[d] * k_t[d];
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
                float norm_buf[4096];
                dequant_row(layer->post_ffw_norm.data, norm_buf, dim,
                            (ggml_type_t)layer->post_ffw_norm.type);
                tensor_rms_norm(state->xb, norm_buf, state->xb, dim, norm_eps);
            }
        }

        /* o. Residual connection */
        residual_add(state->x, state->xb, dim);

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
            float ple_out[4096];
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
    qmatmul_vec(&qm->output_weight, state->x, state->logits,
                c->vocab_size, dim);

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
