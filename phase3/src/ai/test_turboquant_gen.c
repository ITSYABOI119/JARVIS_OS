/*
 * JARVIS AI-OS Phase 3 -- TurboQuant Multi-Step Generation Quality
 *
 * Phase 1: Decompress-overwrite baseline (expected FAIL at ~3.3%)
 * Phase 2: TurboQuantProd — Algorithm 2 with tq_dot_key() scoring
 *
 * SKIPS gracefully if model file not found (exit 0).
 * Requires ~6 GB RAM (F32 model loading).
 *
 * Build:
 *   gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *     phase3/src/ai/test_turboquant_gen.c \
 *     phase3/src/ai/turboquant.c \
 *     phase3/src/ai/llama_forward.c \
 *     phase3/src/ai/llama_forward_tq.c \
 *     phase3/src/ai/llama_load.c \
 *     phase3/src/ai/tensor_ops.c \
 *     phase3/src/ai/dequant.c \
 *     phase3/src/ai/sampling.c \
 *     phase3/src/ai/gguf_parser.c \
 *     phase3/src/ai/tokenizer.c \
 *     -lm -o /tmp/test_turboquant_gen
 */

#include "llama_model.h"
#include "turboquant.h"
#include "llama_forward_tq.h"
#include "gguf_parser.h"
#include "dequant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_PATH "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
#define N_GEN      30
#define EOS_TOKEN  128001
#define MAX_LAYERS 32

/* ---- Helpers (same pattern as test_turboquant_real.c) ---- */

static int greedy_argmax(const float *arr, int n)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (arr[i] > arr[best]) best = i;
    return best;
}

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

/* ---- TQ compress-in-place at a single position ---- */

static void tq_compress_position(llama_state_t *state, const llama_config_t *c,
                                  tq_state_t *tq_states, int pos)
{
    int kv_dim  = c->n_kv_heads * c->head_dim;
    int max_seq = c->max_seq_len;

    for (int L = 0; L < c->n_layers; L++) {
        float *kl = state->key_cache   + L * max_seq * kv_dim;
        float *vl = state->value_cache + L * max_seq * kv_dim;

        for (int h = 0; h < c->n_kv_heads; h++) {
            float *k = kl + pos * kv_dim + h * c->head_dim;
            tq_ckey_t ck;
            tq_compress_key(&tq_states[L], k, &ck);
            tq_decompress_key(&tq_states[L], &ck, k);

            float *v = vl + pos * kv_dim + h * c->head_dim;
            tq_cval_t cv;
            tq_compress_val(&tq_states[L], v, &cv);
            tq_decompress_val(&tq_states[L], &cv, v);
        }
    }
}

/* ============================================================ */

int main(int argc, char **argv)
{
    printf("=== TurboQuant Multi-Step Generation Quality ===\n\n");

    /* Model path: command-line arg or search defaults */
    const char *model_path = NULL;
    if (argc > 1) {
        model_path = argv[1];
    } else {
        static const char *defaults[] = {
            MODEL_PATH,
            "phase3/models/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            NULL
        };
        for (int i = 0; defaults[i]; i++) {
            FILE *probe = fopen(defaults[i], "rb");
            if (probe) { fclose(probe); model_path = defaults[i]; break; }
        }
    }
    if (!model_path) {
        printf("No model found. SKIP (exit 0)\n");
        return 0;
    }

    FILE *f = fopen(model_path, "rb");
    if (!f) {
        printf("Model not found: %s\nSKIP (exit 0)\n", model_path);
        return 0;
    }
    fclose(f);

    /* Load model */
    printf("Loading model... ");
    fflush(stdout);
    llama_model_t model;
    printf("Model: %s\n", model_path);
    if (load_f32_model(&model, model_path) != 0) {
        printf("FAILED\n");
        return 1;
    }

    const llama_config_t *c = &model.config;
    printf("OK (%d layers, %d vocab, head_dim=%d)\n\n",
           c->n_layers, c->vocab_size, c->head_dim);

    /* Init TQ states (one per layer, different seed per layer) */
    tq_state_t tq_states[MAX_LAYERS];
    for (int L = 0; L < c->n_layers && L < MAX_LAYERS; L++)
        tq_init(&tq_states[L], c->head_dim, 3, 3, 42 + (uint64_t)L);

    /* Test prompts — token IDs for Llama 3.2 tokenizer */
    int p1[] = {128000, 791, 513, 43, 19, 8162, 24127, 374};
    int p2[] = {128000, 12805, 5765, 264, 892, 1070, 574};
    int p3[] = {128000, 791, 6864, 315, 8494, 374};

    struct { const char *name; int *tok; int n; } prompts[] = {
        {"The seL4 microkernel is",       p1, 8},
        {"Once upon a time there was",    p2, 7},
        {"The capital of Australia is",   p3, 6},
    };
    int n_prompts = 3;

    float total_match = 0, min_match = 1.0f;
    int pass = 0;

    /* Storage for F32 reference tokens (reused by Phase 2) */
    int f32_all_tokens[3][N_GEN];
    int f32_all_counts[3];

    printf("=== Phase 1: Decompress-Overwrite Baseline ===\n\n");

    for (int p = 0; p < n_prompts; p++) {
        printf("--- Prompt %d: \"%s\" ---\n", p + 1, prompts[p].name);

        int *prompt = prompts[p].tok;
        int n_prompt = prompts[p].n;

        /* ---- F32 reference generation ---- */
        llama_state_t state;
        if (llama_alloc_state(&state, c) != 0) {
            printf("FAILED to alloc state\n");
            goto cleanup;
        }

        for (int i = 0; i < n_prompt; i++)
            llama_forward(&model, &state, prompt[i]);

        int f32_tokens[N_GEN];
        int f32_count = 0;
        for (int i = 0; i < N_GEN; i++) {
            int tok = greedy_argmax(state.logits, c->vocab_size);
            f32_tokens[i] = tok;
            f32_count++;
            if (tok == EOS_TOKEN) break;
            llama_forward(&model, &state, tok);
        }
        llama_free_state(&state);

        /* Save for Phase 2 reuse */
        memcpy(f32_all_tokens[p], f32_tokens, N_GEN * sizeof(int));
        f32_all_counts[p] = f32_count;

        /* ---- TQ generation (compress after every step) ---- */
        if (llama_alloc_state(&state, c) != 0) {
            printf("FAILED to alloc state\n");
            goto cleanup;
        }

        for (int i = 0; i < n_prompt; i++) {
            llama_forward(&model, &state, prompt[i]);
            tq_compress_position(&state, c, tq_states, state.pos - 1);
        }

        int tq_tokens[N_GEN];
        int tq_count = 0;
        for (int i = 0; i < N_GEN; i++) {
            int tok = greedy_argmax(state.logits, c->vocab_size);
            tq_tokens[i] = tok;
            tq_count++;
            if (tok == EOS_TOKEN) break;
            llama_forward(&model, &state, tok);
            tq_compress_position(&state, c, tq_states, state.pos - 1);
        }
        llama_free_state(&state);

        /* ---- Compare ---- */
        int matches = 0, first_div = -1;
        for (int i = 0; i < f32_count && i < tq_count; i++) {
            if (f32_tokens[i] == tq_tokens[i])
                matches++;
            else if (first_div < 0)
                first_div = i;
        }

        float match_rate = (float)matches / (float)f32_count;
        total_match += match_rate;
        if (match_rate < min_match) min_match = match_rate;

        /* Print token sequences */
        printf("F32 (%d tok): [", f32_count);
        for (int i = 0; i < f32_count; i++)
            printf("%d%s", f32_tokens[i], i < f32_count - 1 ? ", " : "");
        printf("]\n");

        printf("TQ  (%d tok): [", tq_count);
        for (int i = 0; i < tq_count; i++)
            printf("%d%s", tq_tokens[i], i < tq_count - 1 ? ", " : "");
        printf("]\n");

        printf("Match: %d/%d tokens (%.1f%%)",
               matches, f32_count, match_rate * 100.0f);
        if (first_div >= 0)
            printf(", first divergence at position %d", first_div);
        printf("\n\n");
    }

    /* ---- Phase 1 Summary (informational only) ---- */
    float avg_match = total_match / (float)n_prompts;

    printf("=== Phase 1 (decompress-overwrite): avg %.1f%%, min %.1f%% ===\n",
           avg_match * 100.0f, min_match * 100.0f);
    printf("(Expected FAIL — documents compounding error limitation)\n\n");

    /* ================================================================
     * Phase 2: TurboQuantProd Bit-Width Sweep
     * Tests multiple key_bits/val_bits configs to find quality threshold.
     * ================================================================ */
    printf("=== Phase 2: TurboQuantProd Bit-Width Sweep ===\n\n");

    struct { int kb; int vb; const char *label; } configs[] = {
        { 3, 3, "3b keys / 3b vals" },
        { 3, 4, "3b keys / 4b vals" },
        { 4, 3, "4b keys / 3b vals" },
        { 4, 4, "4b keys / 4b vals" },
    };
    int n_configs = 4;

    float best_avg = 0;
    int best_cfg = -1;

    float sweep_avgs[4];

    for (int cfg = 0; cfg < n_configs; cfg++) {
        int kb = configs[cfg].kb, vb = configs[cfg].vb;
        printf("--- Config: %s (kb=%d vb=%d) ---\n", configs[cfg].label, kb, vb);

        float cfg_total = 0, cfg_min = 1.0f;

        for (int p = 0; p < n_prompts; p++) {
            llama_tq_state_t tq_st;
            if (llama_tq_alloc_state(&tq_st, c, kb, vb, 42) != 0) {
                printf("  FAILED to alloc TQ state (kb=%d vb=%d)\n", kb, vb);
                goto cleanup;
            }

            int tqp_tokens[N_GEN];
            int tqp_count = llama_tq_generate(&model, &tq_st,
                prompts[p].tok, prompts[p].n,
                tqp_tokens, N_GEN, EOS_TOKEN, 0.0f, 1, 0);

            llama_tq_free_state(&tq_st);

            int f32_count = f32_all_counts[p];
            int matches = 0, first_div = -1;
            int cmp_len = f32_count < tqp_count ? f32_count : tqp_count;
            for (int i = 0; i < cmp_len; i++) {
                if (f32_all_tokens[p][i] == tqp_tokens[i])
                    matches++;
                else if (first_div < 0)
                    first_div = i;
            }

            float mr = (float)matches / (float)f32_count;
            cfg_total += mr;
            if (mr < cfg_min) cfg_min = mr;

            printf("  Prompt %d: %d/%d (%.1f%%)", p + 1, matches, f32_count, mr * 100.0f);
            if (first_div >= 0) printf("  div@%d", first_div);
            printf("\n");
        }

        float cfg_avg = cfg_total / (float)n_prompts;
        sweep_avgs[cfg] = cfg_avg;
        printf("  Avg: %.1f%%  Min: %.1f%%  %s\n\n",
               cfg_avg * 100.0f, cfg_min * 100.0f,
               (cfg_avg >= 0.80f && cfg_min >= 0.60f) ? "PASS" : "FAIL");

        if (cfg_avg > best_avg) { best_avg = cfg_avg; best_cfg = cfg; }
    }

    /* Summary table */
    printf("=== Sweep Summary ===\n");
    for (int cfg = 0; cfg < n_configs; cfg++)
        printf("  %-28s  avg %.1f%%  %s\n", configs[cfg].label,
               sweep_avgs[cfg] * 100.0f,
               sweep_avgs[cfg] >= 0.80f ? "PASS" : "FAIL");
    if (best_cfg >= 0)
        printf("  Best: %s (%.1f%%)\n", configs[best_cfg].label, best_avg * 100.0f);
    printf("\n");

    /* Verdict: use best config */
    pass = (best_avg >= 0.80f) ? 1 : 0;
    if (!pass)
        printf("FAIL: best config %.1f%% < 80%% threshold\n", best_avg * 100.0f);
    printf("Verdict: %s\n", pass ? "PASS" : "FAIL");

cleanup:
    for (int L = 0; L < c->n_layers && L < MAX_LAYERS; L++)
        tq_free(&tq_states[L]);
    llama_free_model(&model);

    return pass ? 0 : 1;
}
