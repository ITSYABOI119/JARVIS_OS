/*
 * JARVIS AI-OS Phase 3 — F32 vs Quantized Forward Pass Comparison
 *
 * Loads the real Llama 3.2 1B model via BOTH paths:
 *   1. F32: manual load (config + alloc + weights with weight-tying fallback)
 *   2. Quantized: gguf_open_memory() + qmodel_load() — zero-copy quantized
 *
 * Runs the same tokens through both and compares intermediate state buffers
 * to pinpoint where quantization divergence (or bugs) occur.
 *
 * Requires: phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf (771 MB)
 * RAM usage: ~6 GB (F32 model) + ~50 MB (quant model) + file buffer
 *
 * Build:
 *   gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *     phase3/src/ai/test_forward_compare.c phase3/src/ai/llama_quant.c \
 *     phase3/src/ai/llama_forward.c phase3/src/ai/llama_load.c \
 *     phase3/src/ai/tensor_ops.c phase3/src/ai/dequant.c \
 *     phase3/src/ai/sampling.c phase3/src/ai/gguf_parser.c \
 *     phase3/src/ai/tokenizer.c -lm -o /tmp/test_forward_compare
 */

#include "llama_model.h"
#include "llama_quant.h"
#include "gguf_parser.h"
#include "dequant.h"
#include "sampling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MODEL_PATH "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"

/* ---- Comparison helper ---- */

static void compare_arrays(const char *name, const float *a, const float *b, int n)
{
    float max_diff = 0;
    int max_idx = 0;
    int mismatches = 0;
    for (int i = 0; i < n; i++) {
        float diff = fabsf(a[i] - b[i]);
        if (diff > max_diff) { max_diff = diff; max_idx = i; }
        if (diff > 0.01f) mismatches++;
    }
    printf("  %-20s max_diff=%.6f at [%d] (f32=%.6f q=%.6f) mismatches=%d/%d",
           name, max_diff, max_idx, a[max_idx], b[max_idx], mismatches, n);
    if (max_diff < 0.1f) printf("  OK\n");
    else if (max_diff < 1.0f) printf("  WARN\n");
    else printf("  DIVERGED!\n");
}

static int argmax(const float *arr, int n)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (arr[i] > arr[best]) best = i;
    return best;
}

/* ---- Load entire file into malloc'd buffer ---- */

static void *load_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return NULL; }

    void *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (rd != (size_t)len) { free(buf); return NULL; }

    *out_len = (size_t)len;
    return buf;
}

/* ---- Helper: load one GGUF tensor into F32 (handles dequant) ---- */

static int load_tensor_to_f32(gguf_ctx_t *ctx, const char *name,
                               float *dst, int n_elements)
{
    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
    if (!t) {
        fprintf(stderr, "load_tensor_to_f32: '%s' not found\n", name);
        return -1;
    }
    if ((int)t->n_elements != n_elements) {
        fprintf(stderr, "load_tensor_to_f32: '%s' has %lu elems, expected %d\n",
                name, (unsigned long)t->n_elements, n_elements);
        return -1;
    }

    if (t->type == GGML_TYPE_F32) {
        return gguf_read_tensor_data(ctx, t, dst);
    }

    /* Quantized: read raw, then dequantize */
    void *tmp = malloc((size_t)t->n_bytes);
    if (!tmp) return -1;

    int err = gguf_read_tensor_data(ctx, t, tmp);
    if (err) { free(tmp); return err; }

    err = dequant_row(tmp, dst, n_elements, (ggml_type_t)t->type);
    free(tmp);
    return err;
}

/* ---- Load F32 model with weight-tying fallback for output.weight ---- */

static int load_f32_model(llama_model_t *model, const char *path)
{
    if (!model || !path) return -1;
    memset(model, 0, sizeof(*model));

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    if (err) {
        fprintf(stderr, "F32 load: gguf_open failed: %s\n", gguf_strerror(err));
        return err;
    }

    err = llama_load_config(&model->config, &ctx);
    if (err) {
        fprintf(stderr, "F32 load: config failed\n");
        gguf_close(&ctx);
        return err;
    }

    err = llama_alloc_model(model);
    if (err) {
        fprintf(stderr, "F32 load: alloc failed\n");
        gguf_close(&ctx);
        return err;
    }

    const llama_config_t *c = &model->config;
    int dim        = c->dim;
    int n_layers   = c->n_layers;
    int n_heads    = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim   = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int vocab_size = c->vocab_size;

    /* Token embedding */
    err = load_tensor_to_f32(&ctx, "token_embd.weight",
                              model->token_embed, vocab_size * dim);
    if (err) goto fail;

    /* Output norm */
    err = load_tensor_to_f32(&ctx, "output_norm.weight",
                              model->output_norm, dim);
    if (err) goto fail;

    /* Output weight — with weight-tying fallback */
    {
        const gguf_tensor_info_t *out_t = gguf_find_tensor(&ctx, "output.weight");
        if (out_t) {
            err = load_tensor_to_f32(&ctx, "output.weight",
                                      model->output_weight, vocab_size * dim);
            if (err) goto fail;
        } else {
            /* Weight tying: copy token_embed to output_weight */
            printf("       (output.weight not found — using weight tying)\n");
            memcpy(model->output_weight, model->token_embed,
                   (size_t)vocab_size * (size_t)dim * sizeof(float));
        }
    }

    /* Per-layer weights */
    char name[128];
    for (int i = 0; i < n_layers; i++) {
        llama_layer_t *L = &model->layers[i];

        snprintf(name, sizeof(name), "blk.%d.attn_norm.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->attn_norm, dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wq, n_heads * head_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wk, n_kv_heads * head_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wv, n_kv_heads * head_dim * dim);
        if (err) goto fail;

        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", i);
        err = load_tensor_to_f32(&ctx, name, L->wo, dim * n_heads * head_dim);
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

        if ((i + 1) % 4 == 0) {
            printf("       ... layer %d/%d loaded\n", i + 1, n_layers);
            fflush(stdout);
        }
    }

    model->loaded = true;
    gguf_close(&ctx);
    return 0;

fail:
    llama_free_model(model);
    gguf_close(&ctx);
    return -1;
}

int main(void)
{
    printf("=== JARVIS F32 vs Quantized Forward Pass Comparison ===\n\n");

    /* Check model file exists */
    {
        FILE *f = fopen(MODEL_PATH, "rb");
        if (!f) {
            printf("SKIP: model not found (%s)\n", MODEL_PATH);
            return 0;
        }
        fclose(f);
    }

    /* ================================================================
     * Step 1: Load F32 model (dequantizes all weights, ~5.7 GB)
     * ================================================================ */
    printf("[1/6] Loading F32 model (this takes ~30-60 seconds)...\n");
    fflush(stdout);

    llama_model_t f32_model;
    int err = load_f32_model(&f32_model, MODEL_PATH);
    if (err) {
        fprintf(stderr, "FAIL: F32 model load returned %d\n", err);
        return 1;
    }
    printf("       F32 model loaded: dim=%d, layers=%d, vocab=%d, %.1f GB\n",
           f32_model.config.dim, f32_model.config.n_layers,
           f32_model.config.vocab_size,
           (double)f32_model.total_bytes / (1024.0 * 1024.0 * 1024.0));
    fflush(stdout);

    /* ================================================================
     * Step 2: Load quantized model (zero-copy pointers into file buffer)
     * ================================================================ */
    printf("[2/6] Loading quantized model (file buffer + zero-copy)...\n");
    fflush(stdout);

    size_t file_len = 0;
    void *file_buf = load_file(MODEL_PATH, &file_len);
    if (!file_buf) {
        fprintf(stderr, "FAIL: could not read model file into memory\n");
        llama_free_model(&f32_model);
        return 1;
    }
    printf("       File loaded: %.1f MB\n", (double)file_len / (1024.0 * 1024.0));

    gguf_ctx_t qctx;
    err = gguf_open_memory(&qctx, file_buf, file_len);
    if (err) {
        fprintf(stderr, "FAIL: gguf_open_memory returned %d (%s)\n",
                err, gguf_strerror(err));
        free(file_buf);
        llama_free_model(&f32_model);
        return 1;
    }

    qmodel_t qmodel;
    err = qmodel_load(&qmodel, &qctx, file_buf);
    if (err) {
        fprintf(stderr, "FAIL: qmodel_load returned %d\n", err);
        gguf_close(&qctx);
        free(file_buf);
        llama_free_model(&f32_model);
        return 1;
    }
    printf("       Quantized model loaded: dim=%d, layers=%d, vocab=%d\n",
           qmodel.config.dim, qmodel.config.n_layers, qmodel.config.vocab_size);
    fflush(stdout);

    /* ================================================================
     * Step 3: Allocate two separate inference states
     * ================================================================ */
    printf("[3/6] Allocating inference states...\n");
    fflush(stdout);

    llama_state_t f32_state, q_state;
    err = llama_alloc_state(&f32_state, &f32_model.config);
    if (err) {
        fprintf(stderr, "FAIL: llama_alloc_state (f32) returned %d\n", err);
        goto cleanup;
    }
    err = llama_alloc_state(&q_state, &qmodel.config);
    if (err) {
        fprintf(stderr, "FAIL: llama_alloc_state (quant) returned %d\n", err);
        llama_free_state(&f32_state);
        goto cleanup;
    }

    const int dim = f32_model.config.dim;
    const int n_heads = f32_model.config.n_heads;
    const int n_kv_heads = f32_model.config.n_kv_heads;
    const int head_dim = f32_model.config.head_dim;
    const int kv_dim = n_kv_heads * head_dim;
    const int vocab_size = f32_model.config.vocab_size;

    printf("       Config: dim=%d, n_heads=%d, n_kv_heads=%d, head_dim=%d, "
           "kv_dim=%d, vocab=%d\n", dim, n_heads, n_kv_heads, head_dim,
           kv_dim, vocab_size);
    fflush(stdout);

    /* ================================================================
     * Step 4: Token 1 — forward pass with token 9906 ("Hello")
     * ================================================================ */
    {
        int token1 = 9906;
        printf("\n[4/6] Forward pass: token %d (\"Hello\"), pos=0\n", token1);
        fflush(stdout);

        /* Save embedding for comparison before layers modify x */
        float *f32_embed = (float *)malloc((size_t)dim * sizeof(float));
        float *q_embed   = (float *)malloc((size_t)dim * sizeof(float));
        if (!f32_embed || !q_embed) {
            fprintf(stderr, "FAIL: embed alloc\n");
            free(f32_embed); free(q_embed);
            llama_free_state(&f32_state); llama_free_state(&q_state);
            goto cleanup;
        }

        /* Copy F32 embedding row */
        memcpy(f32_embed, f32_model.token_embed + (size_t)token1 * dim,
               (size_t)dim * sizeof(float));

        /* Dequantize quantized embedding row */
        {
            const ggml_type_t etype = (ggml_type_t)qmodel.token_embed.type;
            if (etype == GGML_TYPE_F32) {
                const float *ef = (const float *)qmodel.token_embed.data;
                memcpy(q_embed, ef + (size_t)token1 * dim,
                       (size_t)dim * sizeof(float));
            } else {
                size_t row_bytes = dequant_row_bytes(dim, etype);
                const void *row = (const uint8_t *)qmodel.token_embed.data
                                  + (size_t)token1 * row_bytes;
                dequant_row(row, q_embed, dim, etype);
            }
        }

        printf("  --- Embedding (token %d) ---\n", token1);
        compare_arrays("embed", f32_embed, q_embed, dim);
        free(f32_embed);
        free(q_embed);

        /* Run full forward pass on both paths */
        printf("  --- Running full forward pass on both paths ---\n");
        fflush(stdout);

        llama_forward(&f32_model, &f32_state, token1);
        qmodel_forward(&qmodel, &q_state, token1);

        /* Compare final state buffers */
        printf("  --- Post-forward comparison (after all %d layers) ---\n",
               f32_model.config.n_layers);
        compare_arrays("x (residual)", f32_state.x, q_state.x, dim);
        compare_arrays("xb", f32_state.xb, q_state.xb, dim);
        compare_arrays("q", f32_state.q, q_state.q, n_heads * head_dim);
        compare_arrays("k", f32_state.k, q_state.k, kv_dim);
        compare_arrays("v", f32_state.v, q_state.v, kv_dim);
        compare_arrays("logits", f32_state.logits, q_state.logits, vocab_size);

        /* Argmax comparison */
        int f32_argmax = argmax(f32_state.logits, vocab_size);
        int q_argmax   = argmax(q_state.logits, vocab_size);
        printf("\n  Token 1 argmax:  F32=%d (logit=%.4f)  Q=%d (logit=%.4f)  %s\n",
               f32_argmax, f32_state.logits[f32_argmax],
               q_argmax, q_state.logits[q_argmax],
               f32_argmax == q_argmax ? "MATCH" : "MISMATCH");
        fflush(stdout);
    }

    /* ================================================================
     * Step 5: Token 2 — forward pass with token 374 (tests KV cache)
     * ================================================================ */
    {
        int token2 = 374;
        printf("\n[5/6] Forward pass: token %d, pos=1 (tests KV cache)\n", token2);
        fflush(stdout);

        llama_forward(&f32_model, &f32_state, token2);
        qmodel_forward(&qmodel, &q_state, token2);

        printf("  --- Post-forward comparison (token 2, pos=1) ---\n");
        compare_arrays("x (residual)", f32_state.x, q_state.x, dim);
        compare_arrays("xb", f32_state.xb, q_state.xb, dim);
        compare_arrays("q", f32_state.q, q_state.q, n_heads * head_dim);
        compare_arrays("k", f32_state.k, q_state.k, kv_dim);
        compare_arrays("v", f32_state.v, q_state.v, kv_dim);
        compare_arrays("logits", f32_state.logits, q_state.logits, vocab_size);

        int f32_argmax = argmax(f32_state.logits, vocab_size);
        int q_argmax   = argmax(q_state.logits, vocab_size);
        printf("\n  Token 2 argmax:  F32=%d (logit=%.4f)  Q=%d (logit=%.4f)  %s\n",
               f32_argmax, f32_state.logits[f32_argmax],
               q_argmax, q_state.logits[q_argmax],
               f32_argmax == q_argmax ? "MATCH" : "MISMATCH");
        fflush(stdout);
    }

    /* ================================================================
     * Step 6: Extended 10-token prefill + 5-token generation
     * ================================================================ */
    {
        printf("\n[6/8] Extended test: 10-token prefill + 5-token generation\n");
        fflush(stdout);

        /* Reset both states */
        f32_state.pos = 0;
        q_state.pos = 0;
        size_t kv_bytes = (size_t)f32_model.config.n_layers *
            f32_state.max_seq_len * kv_dim * sizeof(float);
        memset(f32_state.key_cache, 0, kv_bytes);
        memset(f32_state.value_cache, 0, kv_bytes);
        memset(q_state.key_cache, 0, kv_bytes);
        memset(q_state.value_cache, 0, kv_bytes);

        /* BOS + "The seL4 microkernel is" (from llama-tokenize) */
        int prompt[] = {128000, 791, 513, 43, 19, 8162, 24127, 374};
        int n_prompt = 8;
        printf("  Prompt: BOS + 'The seL4 microkernel is' (%d tokens)\n", n_prompt);

        for (int i = 0; i < n_prompt; i++) {
            llama_forward(&f32_model, &f32_state, prompt[i]);
            qmodel_forward(&qmodel, &q_state, prompt[i]);
        }

        compare_arrays("logits_10tok", f32_state.logits, q_state.logits, vocab_size);
        int f32_am = argmax(f32_state.logits, vocab_size);
        int q_am = argmax(q_state.logits, vocab_size);
        printf("  After 10 tokens: F32 argmax=%d  Q argmax=%d  %s\n",
               f32_am, q_am, f32_am == q_am ? "MATCH" : "MISMATCH");

        /* Check for the degenerate token */
        printf("  F32 logits[13780]=%.4f  Q logits[13780]=%.4f\n",
               f32_state.logits[13780], q_state.logits[13780]);

        /* Generate 5 tokens from each */
        printf("\n  --- Generation from pos=10 ---\n");
        for (int g = 0; g < 5; g++) {
            int f32_next = argmax(f32_state.logits, vocab_size);
            int q_next = argmax(q_state.logits, vocab_size);
            printf("  gen[%d] f32=%d quant=%d %s\n", g, f32_next, q_next,
                   f32_next == q_next ? "MATCH" : "DIFFER");
            llama_forward(&f32_model, &f32_state, f32_next);
            qmodel_forward(&qmodel, &q_state, q_next);
        }

        /* If BOTH produce repeated tokens, bug is in shared llama_forward logic */
        printf("\n");
        fflush(stdout);
    }

    /* ================================================================
     * Step 7: Summary
     * ================================================================ */
    printf("\n[7/8] Summary\n");
    printf("       Both forward passes completed successfully.\n");
    printf("       Quantization noise is expected (Q4_K_M vs F32).\n");
    printf("       DIVERGED = likely bug; WARN = expected quantization error; "
           "OK = close match.\n");
    printf("       If BOTH paths repeat the same token, bug is in llama_forward.c\n");
    printf("       If only quant repeats, bug is in llama_quant.c\n");

    /* Cleanup */
    llama_free_state(&f32_state);
    llama_free_state(&q_state);

cleanup:
    qmodel_free(&qmodel);
    gguf_close(&qctx);
    free(file_buf);
    llama_free_model(&f32_model);

    printf("\nDone.\n");
    return 0;
}
