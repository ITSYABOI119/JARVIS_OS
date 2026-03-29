# TurboQuantProd: Compressed-Representation Attention Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the paper's Algorithm 2 (TurboQuantProd) — store compressed KV representations directly and use `tq_dot_key()` for attention scoring, fixing the catastrophic 3.3% match rate from decompress-overwrite.

**Architecture:** Copy `llama_forward.c` into `llama_forward_tq.c`, change only 3 sections: (1) KV cache store → TQ compression, (2) attention scoring → `tq_dot_key()`, (3) value weighted sum → on-the-fly decompression. Everything else (embedding, RoPE, FFN, output projection) stays identical. New `llama_tq_state_t` replaces `float *key_cache/value_cache` with `tq_ckey_t*/tq_cval_t*` arrays.

**Tech Stack:** C11, gcc, Llama 3.2 1B Q4_K_M (F32 dequant path), TurboQuant module

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/ai/llama_forward_tq.h` | Create | TQ state struct + API declarations |
| `phase3/src/ai/llama_forward_tq.c` | Create | TQ forward pass + generate (based on llama_forward.c) |
| `phase3/src/ai/test_turboquant_gen.c` | Modify | Add TurboQuantProd comparison after existing decompress-overwrite test |
| `.github/workflows/ci.yml` | Modify | Update TurboQuant Gen CI step to include llama_forward_tq.c |
| `CLAUDE.md` | Modify | Add milestone, update test counts, add quick reference |

**Do NOT modify:** `llama_forward.c`, `llama_quant.c`, `llama_model.h`, `turboquant.h`, `turboquant.c`

---

### Task 1: Create llama_forward_tq.h

**Files:**
- Create: `phase3/src/ai/llama_forward_tq.h`

- [ ] **Step 1: Write the header file**

```c
/*
 * JARVIS AI-OS Phase 3 -- TurboQuant-Integrated Forward Pass (Algorithm 2)
 *
 * Stores compressed tq_ckey_t/tq_cval_t in KV cache.
 * Uses tq_dot_key() for attention scoring (QJL-corrected).
 * Decompresses values on-the-fly for weighted sum.
 * Everything else is identical to llama_forward.c.
 */

#ifndef LLAMA_FORWARD_TQ_H
#define LLAMA_FORWARD_TQ_H

#include "llama_model.h"
#include "turboquant.h"

#define TQ_MAX_LAYERS 32

/* TQ-integrated inference state.
 * Replaces F32 key_cache/value_cache with compressed representations.
 * Activation buffers (x, xb, q, k, v, etc.) remain F32. */
typedef struct {
    /* Activation buffers (identical to llama_state_t) */
    float *x, *xb, *xb2;
    float *q, *k, *v;
    float *att;
    float *hb, *hb2;
    float *logits;

    /* Compressed KV cache: indexed as [L * max_seq * n_kv_heads + pos * n_kv_heads + h] */
    tq_ckey_t *key_cache;   /* n_layers * max_seq * n_kv_heads entries */
    tq_cval_t *val_cache;   /* n_layers * max_seq * n_kv_heads entries */

    /* Per-layer TQ state (rotation matrix Pi + QJL matrix S + codebooks) */
    tq_state_t tq_states[TQ_MAX_LAYERS];

    int pos;
    int max_seq_len;
    int n_layers;
    int n_kv_heads;
} llama_tq_state_t;

/* Allocate TQ state. base_seed is used to derive per-layer seeds (base_seed + L). */
int  llama_tq_alloc_state(llama_tq_state_t *state, const llama_config_t *config,
                           int key_bits, int val_bits, uint64_t base_seed);
void llama_tq_free_state(llama_tq_state_t *state);

/* Forward pass: identical to llama_forward except KV cache is compressed. */
void llama_tq_forward(const llama_model_t *model, llama_tq_state_t *state, int token);

/* Generation: identical to llama_generate but uses llama_tq_forward. */
int  llama_tq_generate(const llama_model_t *model, llama_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k,
                        uint64_t seed);

#endif /* LLAMA_FORWARD_TQ_H */
```

- [ ] **Step 2: Verify it compiles (header-only sanity check)**

Run:
```bash
echo '#include "llama_forward_tq.h"' | gcc -fsyntax-only -x c -std=c11 -I phase3/src/ai - 2>&1
```
Expected: No errors (just warns about unused includes, which is fine).

- [ ] **Step 3: Commit header**

```bash
git add phase3/src/ai/llama_forward_tq.h
git commit -m "feat: add llama_forward_tq.h — TQ-integrated state struct"
```

---

### Task 2: Create llama_forward_tq.c

**Files:**
- Create: `phase3/src/ai/llama_forward_tq.c`

This is a modified copy of `llama_forward.c` (199 lines). Only 3 sections change:
- Section f: KV cache store → TQ compression
- Section g scoring: F32 dot → `tq_dot_key()`
- Section g value sum: F32 read → on-the-fly decompress

- [ ] **Step 1: Write the alloc/free functions**

```c
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
```

- [ ] **Step 2: Write the TQ forward pass**

This is the critical function. Copy `llama_forward()` exactly, then change only sections f and g.

```c
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

    /* 4. Output projection → logits */
    tensor_matmul(model->output_weight, state->x, state->logits,
                  c->vocab_size, dim, 1);

    /* 5. Increment position */
    state->pos++;
}
```

- [ ] **Step 3: Write the TQ generate function**

```c
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
```

- [ ] **Step 4: Compile the new file (no test yet, just check it builds)**

Run:
```bash
gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai -c \
  phase3/src/ai/llama_forward_tq.c -o /dev/null 2>&1
```
Expected: Clean compile, no errors.

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/llama_forward_tq.h phase3/src/ai/llama_forward_tq.c
git commit -m "feat: TurboQuantProd forward pass (Algorithm 2)

Compressed-representation attention: store tq_ckey_t/tq_cval_t in cache,
use tq_dot_key() for scoring (QJL-corrected), decompress values on-the-fly.
Based on llama_forward.c with only 3 sections changed (f, g-score, g-value)."
```

---

### Task 3: Add TurboQuantProd Test to test_turboquant_gen.c

**Files:**
- Modify: `phase3/src/ai/test_turboquant_gen.c`

The existing test documents the decompress-overwrite FAIL (3.3% match). We add a second phase that uses `llama_tq_generate()` and should achieve >= 80% match.

- [ ] **Step 1: Add include and persistent F32 token storage**

At the top of `test_turboquant_gen.c`, add the new include:

```c
/* After existing includes: */
#include "llama_forward_tq.h"
```

In `main()`, before the prompt loop, add storage for F32 reference tokens so Phase 2 can reuse them:

```c
    /* Storage for F32 reference tokens (reused by Phase 2) */
    int f32_all_tokens[3][N_GEN];
    int f32_all_counts[3];
```

Inside the existing prompt loop, after `f32_count` is computed, add:

```c
        /* Save for Phase 2 reuse */
        memcpy(f32_all_tokens[p], f32_tokens, N_GEN * sizeof(int));
        f32_all_counts[p] = f32_count;
```

- [ ] **Step 2: Change Phase 1 verdict to informational-only**

The existing Phase 1 (decompress-overwrite) is expected to FAIL. Change it so it prints results but doesn't determine pass/fail. Replace the existing verdict section (after Phase 1 loop) from:

```c
    pass = 1;
    if (avg_match < 0.80f) {
```

to instead just print the Phase 1 summary without setting `pass`:

```c
    printf("=== Phase 1 (decompress-overwrite): avg %.1f%%, min %.1f%% ===\n",
           avg_match * 100.0f, min_match * 100.0f);
    printf("(Expected FAIL — documents compounding error limitation)\n\n");
```

Remove the Phase 1 pass/fail checks entirely (they set `pass` based on decompress-overwrite which always FAILs).

- [ ] **Step 3: Add Phase 2 — TurboQuantProd generation**

After Phase 1's summary, before the `cleanup:` label, add:

```c
    /* ================================================================
     * Phase 2: TurboQuantProd — compressed-representation attention
     * Uses tq_dot_key() for scoring, on-the-fly value decompression.
     * This is the paper's Algorithm 2.
     * ================================================================ */
    printf("=== Phase 2: TurboQuantProd (Algorithm 2) ===\n\n");

    float tqp_total_match = 0, tqp_min_match = 1.0f;

    for (int p = 0; p < n_prompts; p++) {
        printf("--- TQProd Prompt %d: \"%s\" ---\n", p + 1, prompts[p].name);

        /* Allocate fresh TQ state for each prompt */
        llama_tq_state_t tq_st;
        if (llama_tq_alloc_state(&tq_st, c, 3, 3, 42) != 0) {
            printf("FAILED to alloc TQ state\n");
            goto cleanup;
        }

        int tqp_tokens[N_GEN];
        int tqp_count = llama_tq_generate(&model, &tq_st,
            prompts[p].tok, prompts[p].n,
            tqp_tokens, N_GEN, EOS_TOKEN, 0.0f, 1, 0);

        llama_tq_free_state(&tq_st);

        /* Compare against saved F32 reference */
        int f32_count = f32_all_counts[p];
        int matches = 0, first_div = -1;
        int cmp_len = f32_count < tqp_count ? f32_count : tqp_count;
        for (int i = 0; i < cmp_len; i++) {
            if (f32_all_tokens[p][i] == tqp_tokens[i])
                matches++;
            else if (first_div < 0)
                first_div = i;
        }

        float match_rate = (float)matches / (float)f32_count;
        tqp_total_match += match_rate;
        if (match_rate < tqp_min_match) tqp_min_match = match_rate;

        printf("F32  (%d tok): [", f32_count);
        for (int i = 0; i < f32_count; i++)
            printf("%d%s", f32_all_tokens[p][i], i < f32_count - 1 ? ", " : "");
        printf("]\n");

        printf("TQP  (%d tok): [", tqp_count);
        for (int i = 0; i < tqp_count; i++)
            printf("%d%s", tqp_tokens[i], i < tqp_count - 1 ? ", " : "");
        printf("]\n");

        printf("Match: %d/%d tokens (%.1f%%)",
               matches, f32_count, match_rate * 100.0f);
        if (first_div >= 0)
            printf(", first divergence at position %d", first_div);
        printf("\n\n");
    }

    float tqp_avg = tqp_total_match / (float)n_prompts;

    printf("=== TurboQuantProd: avg match rate %.1f%%, min match rate %.1f%% ===\n",
           tqp_avg * 100.0f, tqp_min_match * 100.0f);
```

- [ ] **Step 4: Update final verdict to use Phase 2 results**

Replace the existing verdict block with one that uses Phase 2 (TurboQuantProd) results:

```c
    /* Final verdict based on TurboQuantProd (Phase 2) */
    pass = 1;
    if (tqp_avg < 0.80f) {
        printf("FAIL: TurboQuantProd avg match %.1f%% < 80%%\n", tqp_avg * 100.0f);
        pass = 0;
    }
    if (tqp_min_match < 0.60f) {
        printf("FAIL: TurboQuantProd min match %.1f%% < 60%%\n", tqp_min_match * 100.0f);
        pass = 0;
    }

    printf("Verdict: %s\n", pass ? "PASS" : "FAIL");
```

Remove the old decompress-overwrite note (the `if (!pass)` block about QJL correction) since Phase 2 fixes that.

- [ ] **Step 5: Compile the full test**

Run:
```bash
gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
  phase3/src/ai/test_turboquant_gen.c \
  phase3/src/ai/turboquant.c \
  phase3/src/ai/llama_forward.c \
  phase3/src/ai/llama_forward_tq.c \
  phase3/src/ai/llama_load.c \
  phase3/src/ai/tensor_ops.c \
  phase3/src/ai/dequant.c \
  phase3/src/ai/sampling.c \
  phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c \
  -lm -o /tmp/test_turboquant_gen 2>&1
```
Expected: Clean compile, no errors.

- [ ] **Step 6: Run the test**

Run:
```bash
/tmp/test_turboquant_gen
```

Expected output structure:
```
=== TurboQuant Multi-Step Generation Quality ===

Loading model... OK (16 layers, 128256 vocab, head_dim=64)

--- Prompt 1: "The seL4 microkernel is" ---
[Phase 1 decompress-overwrite results — expected FAIL]

=== Phase 1 (decompress-overwrite): avg 3.3%, min 3.3% ===
(Expected FAIL — documents compounding error limitation)

=== Phase 2: TurboQuantProd (Algorithm 2) ===

--- TQProd Prompt 1: "The seL4 microkernel is" ---
[Should show much higher match rate]

=== TurboQuantProd: avg match rate XX.X%, min match rate XX.X% ===
Verdict: PASS/FAIL
```

If FAIL: document actual numbers honestly. Investigate whether `tq_dot_key()` is being called correctly.

- [ ] **Step 7: Commit**

```bash
git add phase3/src/ai/test_turboquant_gen.c
git commit -m "test: add TurboQuantProd comparison to generation quality test

Phase 1: decompress-overwrite (3.3% match — documents limitation)
Phase 2: TurboQuantProd with tq_dot_key() scoring (XX% match)

Verdict based on Phase 2 only."
```

---

### Task 4: Update CI, CLAUDE.md, Push

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update CI step to include llama_forward_tq.c**

In `.github/workflows/ci.yml`, find the existing "Phase 3: TurboQuant Generation Quality (C)" step and replace it with:

```yaml
    - name: "Phase 3: TurboQuant Generation Quality (C)"
      run: |
        gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
          -I phase3/src/ai \
          phase3/src/ai/test_turboquant_gen.c \
          phase3/src/ai/turboquant.c \
          phase3/src/ai/llama_forward.c \
          phase3/src/ai/llama_forward_tq.c \
          phase3/src/ai/llama_load.c \
          phase3/src/ai/tensor_ops.c \
          phase3/src/ai/dequant.c \
          phase3/src/ai/sampling.c \
          phase3/src/ai/gguf_parser.c \
          phase3/src/ai/tokenizer.c \
          -lm -o /tmp/test_turboquant_gen
        /tmp/test_turboquant_gen
```

(Only change from existing: added `phase3/src/ai/llama_forward_tq.c \` line.)

- [ ] **Step 2: Verify CI step compiles locally**

Run the exact CI command from step 1. Expected: Clean compile.

- [ ] **Step 3: Update CLAUDE.md**

Add to Phase 3 milestones table:
```
| **TurboQuantProd attention (Algorithm 2, match rate XX%)** | **DONE** |
```

Add to Early Work table:
```
| TurboQuantProd forward pass | — | DONE | llama_forward_tq.c/h | SKIP on CI |
```

Update test count: 336 → 337 in Early Work total and Codebase Metrics.

Add to Quick Reference:
```
- **TurboQuant Forward Pass:** `phase3/src/ai/llama_forward_tq.c/h`
```

- [ ] **Step 4: Commit and push**

```bash
git add .github/workflows/ci.yml CLAUDE.md
git commit -m "docs: update CI and CLAUDE.md for TurboQuantProd

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"

git push origin experiment/turboquant-benchmark
```

Do NOT commit `.claude/settings.local.json`.

---

## Expected Outcomes

| Metric | Decompress-Overwrite | TurboQuantProd |
|--------|:--------------------:|:--------------:|
| Avg match rate | 3.3% (FAIL) | >= 80% (target) |
| Min match rate | 3.3% (FAIL) | >= 60% (target) |
| QJL correction | Lost (MSE-only) | Preserved (tq_dot_key) |
| Cache storage | F32 (32 MB) | Compressed (~4 MB) |

**If TurboQuantProd still FAILs:** The issue may be value decompression error compounding (values don't have QJL). Document honestly and investigate whether value quantization bits need to be increased (e.g., 4-bit values instead of 3-bit).
