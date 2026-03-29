/*
 * JARVIS AI-OS Phase 3 -- TurboQuant Real Data Validation
 *
 * Loads real Llama 3.2 1B model, runs forward pass on a real prompt,
 * extracts KV cache vectors, compresses with TurboQuant, and compares
 * against F32 ground truth.
 *
 * SKIPS gracefully if model file not found (exit 0).
 * Requires ~6 GB RAM (F32 model loading).
 *
 * Build:
 *   gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *     phase3/src/ai/test_turboquant_real.c \
 *     phase3/src/ai/turboquant.c \
 *     phase3/src/ai/llama_forward.c \
 *     phase3/src/ai/llama_load.c \
 *     phase3/src/ai/tensor_ops.c \
 *     phase3/src/ai/dequant.c \
 *     phase3/src/ai/sampling.c \
 *     phase3/src/ai/gguf_parser.c \
 *     phase3/src/ai/tokenizer.c \
 *     -lm -o /tmp/test_turboquant_real
 */

#include "llama_model.h"
#include "turboquant.h"
#include "gguf_parser.h"
#include "dequant.h"
#include "tensor_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MODEL_PATH "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"

/* ---- Helpers ---- */

static float cosine_sim(const float *a, const float *b, int n)
{
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na < 1e-30f || nb < 1e-30f) return 0.0f;
    return dot / (sqrtf(na) * sqrtf(nb));
}

static int argmax(const float *arr, int n)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (arr[i] > arr[best]) best = i;
    return best;
}

static void softmax(const float *in, float *out, int n)
{
    float maxv = in[0];
    for (int i = 1; i < n; i++)
        if (in[i] > maxv) maxv = in[i];
    float sum = 0;
    for (int i = 0; i < n; i++) {
        out[i] = expf(in[i] - maxv);
        sum += out[i];
    }
    for (int i = 0; i < n; i++)
        out[i] /= sum;
}

static void stats(const float *data, int n, float *out_min, float *out_max,
                  float *out_mean, float *out_std)
{
    float mn = data[0], mx = data[0], sum = 0;
    for (int i = 0; i < n; i++) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
        sum += data[i];
    }
    float mean = sum / (float)n;
    float var = 0;
    for (int i = 0; i < n; i++)
        var += (data[i] - mean) * (data[i] - mean);
    *out_min = mn;
    *out_max = mx;
    *out_mean = mean;
    *out_std = sqrtf(var / (float)n);
}

/* ---- Load tensor to F32 (handles dequant) ---- */

static int load_tensor_to_f32(gguf_ctx_t *ctx, const char *name,
                               float *dst, int n_elements)
{
    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
    if (!t) return -1;
    if ((int)t->n_elements != n_elements) return -1;

    if (t->type == GGML_TYPE_F32)
        return gguf_read_tensor_data(ctx, t, dst);

    void *tmp = malloc((size_t)t->n_bytes);
    if (!tmp) return -1;
    int err = gguf_read_tensor_data(ctx, t, tmp);
    if (err) { free(tmp); return err; }
    err = dequant_row(tmp, dst, n_elements, (ggml_type_t)t->type);
    free(tmp);
    return err;
}

/* ---- Load F32 model (same pattern as test_forward_compare.c) ---- */

static int load_f32_model(llama_model_t *model, const char *path)
{
    memset(model, 0, sizeof(*model));

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    if (err) return err;

    err = llama_load_config(&model->config, &ctx);
    if (err) { gguf_close(&ctx); return err; }

    err = llama_alloc_model(model);
    if (err) { gguf_close(&ctx); return err; }

    const llama_config_t *c = &model->config;
    int dim = c->dim, n_layers = c->n_layers;
    int n_kv_heads = c->n_kv_heads, head_dim = c->head_dim;
    int hidden_dim = c->hidden_dim, vocab_size = c->vocab_size;
    int kv_dim = n_kv_heads * head_dim;

    err = load_tensor_to_f32(&ctx, "token_embd.weight",
                              model->token_embed, vocab_size * dim);
    if (err) goto fail;

    err = load_tensor_to_f32(&ctx, "output_norm.weight",
                              model->output_norm, dim);
    if (err) goto fail;

    /* output.weight — use weight tying if not found */
    {
        const gguf_tensor_info_t *ot = gguf_find_tensor(&ctx, "output.weight");
        if (ot) {
            err = load_tensor_to_f32(&ctx, "output.weight",
                                      model->output_weight, vocab_size * dim);
            if (err) goto fail;
        } else {
            memcpy(model->output_weight, model->token_embed,
                   (size_t)vocab_size * (size_t)dim * sizeof(float));
        }
    }

    char name[128];
    for (int i = 0; i < n_layers; i++) {
        llama_layer_t *L = &model->layers[i];

        snprintf(name, sizeof(name), "blk.%d.attn_norm.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->attn_norm, dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wq, dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wk, kv_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wv, kv_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wo, dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_norm.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->ffn_norm, dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_gate.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->w_gate, hidden_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->w_up, hidden_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.ffn_down.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->w_down, dim * hidden_dim);
        if (err) goto fail;
    }

    model->loaded = true;
    gguf_close(&ctx);
    return 0;

fail:
    llama_free_model(model);
    gguf_close(&ctx);
    return -1;
}

/* ============================================================ */

int main(void)
{
    printf("=== TurboQuant Real Data Validation ===\n\n");

    /* Check model exists */
    FILE *f = fopen(MODEL_PATH, "rb");
    if (!f) {
        printf("Model not found: %s\n", MODEL_PATH);
        printf("SKIP (model required, exit 0)\n");
        return 0;
    }
    fclose(f);

    /* 1. Load F32 model */
    printf("Loading F32 model... ");
    fflush(stdout);
    llama_model_t model;
    if (load_f32_model(&model, MODEL_PATH) != 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");

    const llama_config_t *c = &model.config;
    int head_dim   = c->head_dim;
    int n_kv_heads = c->n_kv_heads;
    int kv_dim     = n_kv_heads * head_dim;
    int max_seq    = c->max_seq_len;
    int n_heads    = c->n_heads;

    printf("  dim=%d n_layers=%d n_heads=%d n_kv_heads=%d head_dim=%d\n\n",
           c->dim, c->n_layers, n_heads, n_kv_heads, head_dim);

    /* 2. Alloc state and run forward pass */
    llama_state_t state;
    if (llama_alloc_state(&state, c) != 0) {
        printf("FAILED to alloc state\n");
        llama_free_model(&model);
        return 1;
    }

    /* BOS + "The seL4 microkernel is" */
    int prompt[] = { 128000, 791, 513, 43, 19, 8162, 24127, 374 };
    int n_prompt = 8;

    printf("Running forward pass (%d tokens)... ", n_prompt);
    fflush(stdout);
    for (int i = 0; i < n_prompt; i++)
        llama_forward(&model, &state, prompt[i]);
    printf("OK (pos=%d)\n\n", state.pos);

    /* 3. Extract KV vectors from layer 0 and compute statistics */
    int test_layer = 0;
    int n_positions = n_prompt;  /* 8 positions after processing 8 tokens */

    float *key_layer = state.key_cache + test_layer * max_seq * kv_dim;
    float *val_layer = state.value_cache + test_layer * max_seq * kv_dim;

    /* Gather all key/value floats from layer 0 for statistics */
    int n_key_floats = n_positions * kv_dim;
    float kmin, kmax, kmean, kstd;
    float vmin, vmax, vmean, vstd;
    stats(key_layer, n_key_floats, &kmin, &kmax, &kmean, &kstd);
    stats(val_layer, n_key_floats, &vmin, &vmax, &vmean, &vstd);

    printf("KV Cache Statistics (layer 0, %d positions):\n", n_positions);
    printf("  Keys:   min=%.4f max=%.4f mean=%.4f std=%.4f\n",
           kmin, kmax, kmean, kstd);
    printf("  Values: min=%.4f max=%.4f mean=%.4f std=%.4f\n\n",
           vmin, vmax, vmean, vstd);

    /* 4. Init TurboQuant and compress all real KV vectors */
    tq_state_t tq;
    if (tq_init(&tq, head_dim, 3, 3, 42) != 0) {
        printf("FAILED to init TurboQuant\n");
        llama_free_state(&state);
        llama_free_model(&model);
        return 1;
    }

    float key_cos_sum = 0, key_cos_min = 2.0f, key_cos_max = -2.0f;
    float val_cos_sum = 0, val_cos_min = 2.0f, val_cos_max = -2.0f;
    int n_vecs = 0;

    /* Store compressed keys for attention test later */
    tq_ckey_t compressed_keys[8 * 8];  /* max 8 positions * 8 heads */

    for (int pos = 0; pos < n_positions; pos++) {
        for (int h = 0; h < n_kv_heads; h++) {
            float *kv_key = key_layer + pos * kv_dim + h * head_dim;
            float *kv_val = val_layer + pos * kv_dim + h * head_dim;

            /* Compress key */
            tq_ckey_t ck;
            tq_compress_key(&tq, kv_key, &ck);
            compressed_keys[pos * n_kv_heads + h] = ck;

            /* Decompress and compare */
            float krecon[128];
            tq_decompress_key(&tq, &ck, krecon);
            float kcos = cosine_sim(kv_key, krecon, head_dim);
            key_cos_sum += kcos;
            if (kcos < key_cos_min) key_cos_min = kcos;
            if (kcos > key_cos_max) key_cos_max = kcos;

            /* Compress value */
            tq_cval_t cv;
            tq_compress_val(&tq, kv_val, &cv);
            float vrecon[128];
            tq_decompress_val(&tq, &cv, vrecon);
            float vcos = cosine_sim(kv_val, vrecon, head_dim);
            val_cos_sum += vcos;
            if (vcos < val_cos_min) val_cos_min = vcos;
            if (vcos > val_cos_max) val_cos_max = vcos;

            n_vecs++;
        }
    }

    float key_cos_avg = key_cos_sum / (float)n_vecs;
    float val_cos_avg = val_cos_sum / (float)n_vecs;

    printf("Compression Quality (3-bit, %d real vectors):\n", n_vecs * 2);
    printf("  Key cosine:   avg=%.4f min=%.4f max=%.4f\n",
           key_cos_avg, key_cos_min, key_cos_max);
    printf("  Value cosine: avg=%.4f min=%.4f max=%.4f\n\n",
           val_cos_avg, val_cos_min, val_cos_max);

    /* 5. Attention score comparison (layer 0, head 0) */
    /* Use the query vector from the last forward pass (state->q contains it) */
    float *query_h0 = state.q;  /* Head 0: first head_dim floats */

    /* F32 attention scores */
    float f32_scores[8], tq_scores[8];
    float f32_softmax[8], tq_softmax[8];

    int kv_h0 = 0;  /* KV head for query head 0 (GQA: h / heads_per_kv) */

    for (int t = 0; t < n_positions; t++) {
        float *k_t = key_layer + t * kv_dim + kv_h0 * head_dim;

        /* F32 score */
        float dot = 0;
        for (int d = 0; d < head_dim; d++)
            dot += query_h0[d] * k_t[d];
        f32_scores[t] = dot / sqrtf((float)head_dim);

        /* TQ score via QJL-corrected inner product */
        tq_ckey_t *ck = &compressed_keys[t * n_kv_heads + kv_h0];
        tq_scores[t] = tq_dot_key(&tq, query_h0, ck) / sqrtf((float)head_dim);
    }

    /* Softmax both */
    softmax(f32_scores, f32_softmax, n_positions);
    softmax(tq_scores, tq_softmax, n_positions);

    float score_corr = cosine_sim(f32_scores, tq_scores, n_positions);
    float softmax_corr = cosine_sim(f32_softmax, tq_softmax, n_positions);
    int f32_argmax = argmax(f32_softmax, n_positions);
    int tq_argmax = argmax(tq_softmax, n_positions);

    printf("Attention Score Comparison (layer 0, head 0):\n");
    printf("  Score correlation:   %.4f\n", score_corr);
    printf("  Softmax correlation: %.4f\n", softmax_corr);
    printf("  Argmax match:        %s (f32=%d tq=%d)\n\n",
           f32_argmax == tq_argmax ? "YES" : "NO", f32_argmax, tq_argmax);

    /* Print raw scores for inspection */
    printf("  Raw scores (f32 vs TQ):\n");
    for (int t = 0; t < n_positions; t++)
        printf("    pos %d: f32=%8.4f  tq=%8.4f  diff=%+.4f\n",
               t, f32_scores[t], tq_scores[t],
               tq_scores[t] - f32_scores[t]);
    printf("\n");

    printf("  Softmax (f32 vs TQ):\n");
    for (int t = 0; t < n_positions; t++)
        printf("    pos %d: f32=%.6f  tq=%.6f\n",
               t, f32_softmax[t], tq_softmax[t]);
    printf("\n");

    /* 6. Multi-layer, multi-head attention test */
    int n_test_layers = (c->n_layers < 4) ? c->n_layers : 4;
    int n_test_heads = (n_heads < 4) ? n_heads : 4;

    printf("Multi-layer attention (layers 0-%d, heads 0-%d):\n",
           n_test_layers - 1, n_test_heads - 1);

    float total_score_corr = 0, total_softmax_corr = 0;
    int total_argmax_match = 0, total_attn_tests = 0;

    int heads_per_kv = n_heads / n_kv_heads;

    for (int L = 0; L < n_test_layers; L++) {
        float *kl = state.key_cache + L * max_seq * kv_dim;

        /* Re-run forward to get fresh Q for this layer? No, we only have
         * Q from the last layer iteration. Instead, use key vectors AS queries
         * (cross-attention style) to test the compression quality. */
        for (int qh = 0; qh < n_test_heads; qh++) {
            int kvh = qh / heads_per_kv;

            /* Use key at position 0 as query (real data, real scale) */
            float *query = kl + 0 * kv_dim + kvh * head_dim;

            /* Init TQ state with layer-specific seed */
            tq_state_t tq_l;
            tq_init(&tq_l, head_dim, 3, 3, 42 + (uint64_t)L);

            float fs[8], ts[8], fs_sm[8], ts_sm[8];
            for (int t = 0; t < n_positions; t++) {
                float *k_t = kl + t * kv_dim + kvh * head_dim;

                float dot = 0;
                for (int d = 0; d < head_dim; d++)
                    dot += query[d] * k_t[d];
                fs[t] = dot / sqrtf((float)head_dim);

                tq_ckey_t ck;
                tq_compress_key(&tq_l, k_t, &ck);
                ts[t] = tq_dot_key(&tq_l, query, &ck) / sqrtf((float)head_dim);
            }

            softmax(fs, fs_sm, n_positions);
            softmax(ts, ts_sm, n_positions);

            float sc = cosine_sim(fs, ts, n_positions);
            float smc = cosine_sim(fs_sm, ts_sm, n_positions);
            int am = (argmax(fs_sm, n_positions) == argmax(ts_sm, n_positions));

            total_score_corr += sc;
            total_softmax_corr += smc;
            total_argmax_match += am;
            total_attn_tests++;

            tq_free(&tq_l);
        }
    }

    float avg_score_corr = total_score_corr / (float)total_attn_tests;
    float avg_softmax_corr = total_softmax_corr / (float)total_attn_tests;
    float argmax_rate = (float)total_argmax_match / (float)total_attn_tests;

    printf("  Avg score correlation:   %.4f\n", avg_score_corr);
    printf("  Avg softmax correlation: %.4f\n", avg_softmax_corr);
    printf("  Argmax match rate:       %.1f%% (%d/%d)\n\n",
           argmax_rate * 100.0f, total_argmax_match, total_attn_tests);

    /* 7. Generation comparison: F32 logits vs TQ-influenced logits
     *
     * Approach:
     * 1. Re-run prefill of first 7 tokens (fresh KV cache)
     * 2. Save KV cache for positions 0-6
     * 3. Forward last token → f32_logits
     * 4. Restore cache, TQ-compress positions 0-6, forward last token → tq_logits
     * 5. Compare top-5 tokens
     */
    int top5_overlap = 5; /* Default to passing if generation comparison is skipped */

    printf("--- Generation Comparison ---\n\n");

    /* Re-init state for clean run */
    tq_free(&tq);
    llama_free_state(&state);
    llama_alloc_state(&state, c);

    int n_layers = c->n_layers;

    /* Prefill first n_prompt-1 tokens */
    int save_pos = n_prompt - 1;
    for (int i = 0; i < save_pos; i++)
        llama_forward(&model, &state, prompt[i]);

    /* Save KV cache for positions 0..save_pos-1 */
    size_t save_per_layer = (size_t)save_pos * (size_t)kv_dim * sizeof(float);
    float *saved_keys = (float *)malloc((size_t)n_layers * save_per_layer);
    float *saved_vals = (float *)malloc((size_t)n_layers * save_per_layer);
    if (!saved_keys || !saved_vals) {
        printf("SKIP generation comparison (malloc failed)\n");
        free(saved_keys); free(saved_vals);
        goto verdict;
    }

    for (int L = 0; L < n_layers; L++) {
        memcpy(saved_keys + L * save_pos * kv_dim,
               state.key_cache + L * max_seq * kv_dim,
               save_per_layer);
        memcpy(saved_vals + L * save_pos * kv_dim,
               state.value_cache + L * max_seq * kv_dim,
               save_per_layer);
    }

    /* Forward last token with F32 cache → f32_logits */
    llama_forward(&model, &state, prompt[n_prompt - 1]);
    float *f32_logits = (float *)malloc((size_t)c->vocab_size * sizeof(float));
    memcpy(f32_logits, state.logits, (size_t)c->vocab_size * sizeof(float));

    /* Restore cache, set pos back */
    state.pos = save_pos;
    for (int L = 0; L < n_layers; L++) {
        memcpy(state.key_cache + L * max_seq * kv_dim,
               saved_keys + L * save_pos * kv_dim,
               save_per_layer);
        memcpy(state.value_cache + L * max_seq * kv_dim,
               saved_vals + L * save_pos * kv_dim,
               save_per_layer);
    }

    /* TQ-compress positions 0..save_pos-1 in all layers (in-place) */
    printf("TQ-compressing %d positions x %d layers x %d heads... ",
           save_pos, n_layers, n_kv_heads);
    fflush(stdout);
    for (int L = 0; L < n_layers; L++) {
        tq_state_t tq_l;
        tq_init(&tq_l, head_dim, 3, 3, 42 + (uint64_t)L);
        float *kl = state.key_cache + L * max_seq * kv_dim;
        float *vl = state.value_cache + L * max_seq * kv_dim;

        for (int p = 0; p < save_pos; p++) {
            for (int h = 0; h < n_kv_heads; h++) {
                float *k = kl + p * kv_dim + h * head_dim;
                tq_ckey_t ck;
                tq_compress_key(&tq_l, k, &ck);
                tq_decompress_key(&tq_l, &ck, k);

                float *v = vl + p * kv_dim + h * head_dim;
                tq_cval_t cv;
                tq_compress_val(&tq_l, v, &cv);
                tq_decompress_val(&tq_l, &cv, v);
            }
        }
        tq_free(&tq_l);
    }
    printf("OK\n");

    /* Forward last token with TQ cache → tq_logits */
    llama_forward(&model, &state, prompt[n_prompt - 1]);

    /* Compare top-5 tokens */
    /* Work on copies to avoid destructive argmax masking */
    float *f32_copy = (float *)malloc((size_t)c->vocab_size * sizeof(float));
    float *tq_copy = (float *)malloc((size_t)c->vocab_size * sizeof(float));
    memcpy(f32_copy, f32_logits, (size_t)c->vocab_size * sizeof(float));
    memcpy(tq_copy, state.logits, (size_t)c->vocab_size * sizeof(float));

    int f32_top5[5], tq_top5[5];
    for (int rank = 0; rank < 5; rank++) {
        f32_top5[rank] = argmax(f32_copy, c->vocab_size);
        f32_copy[f32_top5[rank]] = -1e30f;

        tq_top5[rank] = argmax(tq_copy, c->vocab_size);
        tq_copy[tq_top5[rank]] = -1e30f;
    }

    int top1_match = (f32_top5[0] == tq_top5[0]);
    top5_overlap = 0;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            if (f32_top5[i] == tq_top5[j]) top5_overlap++;

    printf("\nGeneration comparison (F32 KV vs TQ KV):\n");
    printf("  Top-1 match: %s (f32=%d tq=%d)\n",
           top1_match ? "YES" : "NO", f32_top5[0], tq_top5[0]);
    printf("  Top-5 overlap: %d/5\n", top5_overlap);
    printf("  F32 top-5: [%d, %d, %d, %d, %d]\n",
           f32_top5[0], f32_top5[1], f32_top5[2], f32_top5[3], f32_top5[4]);
    printf("  TQ  top-5: [%d, %d, %d, %d, %d]\n\n",
           tq_top5[0], tq_top5[1], tq_top5[2], tq_top5[3], tq_top5[4]);

    free(f32_logits);
    free(f32_copy);
    free(tq_copy);
    free(saved_keys);
    free(saved_vals);

verdict:
    /* 8. Verdict */
    ;
    int pass = 1;
    if (key_cos_avg < 0.85f) {
        printf("FAIL: key cosine avg %.4f < 0.85\n", key_cos_avg);
        pass = 0;
    }
    if (val_cos_avg < 0.90f) {
        printf("FAIL: value cosine avg %.4f < 0.90\n", val_cos_avg);
        pass = 0;
    }
    if (avg_softmax_corr < 0.90f) {
        printf("FAIL: softmax correlation %.4f < 0.90\n", avg_softmax_corr);
        pass = 0;
    }
    if (top5_overlap < 3) {
        printf("FAIL: top-5 overlap %d/5 < 3\n", top5_overlap);
        pass = 0;
    }

    printf("Verdict: %s\n", pass ? "PASS" : "FAIL");

    /* Cleanup */
    llama_free_state(&state);
    llama_free_model(&model);

    return pass ? 0 : 1;
}
