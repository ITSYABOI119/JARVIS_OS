# TurboQuant Real Compressed Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `qmodel_tq_forward()` — a forward pass combining quantized weight matmul (from `llama_quant.c`) with TurboQuant compressed KV cache (from `llama_forward_tq.c`), then wire it into the seL4 inference server with a runtime toggle.

**Architecture:** New files `qmodel_tq_forward.c/h` combine `qmatmul_vec()` (on-the-fly weight dequant) with `tq_compress_key/val` + `tq_dot_key` + `tq_decompress_val` (compressed KV cache). A new `qmodel_tq_state_t` struct holds activation buffers + compressed KV cache + per-layer TQ states (dynamically allocated). Process B gets a `MSG_SET_TQ_MODE` (0x0F) IPC message to toggle between standard and TQ paths at runtime.

**Tech Stack:** C11, seL4 musl, TurboQuant (turboquant.c), quantized inference (llama_quant.c), GGUF zero-copy model loading

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `phase3/src/ai/qmodel_tq_forward.h` | Public API: qmodel_tq_state_t struct, alloc/free/forward/generate |
| `phase3/src/ai/qmodel_tq_forward.c` | Forward pass: quantized weights + TQ compressed KV cache |
| `phase3/src/ai/qmatmul.h` | Extracted shared functions: qmatmul_vec, qembed_lookup (from llama_quant.c) |
| `phase3/src/ai/test_qmodel_tq.c` | Unit tests for the combined forward pass |

### Modified Files

| File | Change |
|------|--------|
| `phase3/src/ai/llama_quant.c` | Make qmatmul_vec and qembed_lookup non-static (remove `static` keyword) |
| `phase3/src/sel4/inference_server.c` | Add MSG_SET_TQ_MODE handler, TQ forward path |
| `phase3/src/sel4/main_x86.c` | Add comparison demo: standard → enable TQ → same query → print both |
| `phase3/src/ipc/shmem_ipc.h` | Add MSG_SET_TQ_MODE 0x0F |
| `.github/workflows/ci.yml` | Add CI step for test_qmodel_tq.c |
| `CLAUDE.md` | Update status table, test count, Quick Reference |

---

## Task 1: Extract qmatmul_vec and qembed_lookup to Shared Header

**Files:**
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/qmatmul.h`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/llama_quant.c` (lines 68-145)

Currently `qmatmul_vec()` (line 68) and `qembed_lookup()` (line 129) are `static` in `llama_quant.c`. The new `qmodel_tq_forward.c` needs them. Rather than duplicating code, make them non-static and declare them in a shared header.

- [ ] **Step 1: Read llama_quant.c to confirm exact signatures**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
head -150 phase3/src/ai/llama_quant.c
```

Confirm these exact signatures at lines 68 and 129:
```c
static void qmatmul_vec(const qtensor_t *W, const float *x, float *out, int M, int K);
static void qembed_lookup(const qtensor_t *embed, int token, float *out, int dim);
```

- [ ] **Step 2: Create qmatmul.h**

```c
/* qmatmul.h — Shared quantized matmul and embedding functions
 * Extracted from llama_quant.c for reuse in qmodel_tq_forward.c
 */
#ifndef QMATMUL_H
#define QMATMUL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "llama_quant.h"

/* Quantized matrix-vector multiply: W[M x K] @ x[K] -> out[M]
 * Dequantizes on-the-fly in 256-element blocks on the stack. */
void qmatmul_vec(const qtensor_t *W, const float *x, float *out, int M, int K);

/* Look up one token embedding from quantized embedding table.
 * embed is [vocab_size x dim] quantized, token selects row. */
void qembed_lookup(const qtensor_t *embed, int token, float *out, int dim);

#ifdef __cplusplus
}
#endif

#endif /* QMATMUL_H */
```

- [ ] **Step 3: Remove `static` from both functions in llama_quant.c**

In `llama_quant.c`, line 68, change:
```c
static void qmatmul_vec(const qtensor_t *W, const float *x, float *out,
```
to:
```c
void qmatmul_vec(const qtensor_t *W, const float *x, float *out,
```

At line 129, change:
```c
static void qembed_lookup(const qtensor_t *embed, int token, float *out,
```
to:
```c
void qembed_lookup(const qtensor_t *embed, int token, float *out,
```

Add `#include "qmatmul.h"` after the existing includes (line 33).

- [ ] **Step 4: Verify existing tests still pass**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai
gcc -Wall -Werror -O2 -std=c11 -I. \
  test_llama_quant.c llama_quant.c llama_load.c gguf_parser.c \
  dequant.c tensor_ops.c sampling.c tokenizer.c \
  -lm -o /tmp/test_llama_quant && /tmp/test_llama_quant
```

Expected: All existing tests PASS. The functions are now non-static but the signatures are identical — no behavioral change.

- [ ] **Step 5: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/qmatmul.h phase3/src/ai/llama_quant.c
git commit -m "refactor: extract qmatmul_vec/qembed_lookup to shared header

Make qmatmul_vec() and qembed_lookup() non-static in llama_quant.c
and declare them in qmatmul.h for reuse by qmodel_tq_forward.c.
No behavioral change — signatures identical.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Create qmodel_tq_state_t and Alloc/Free

**Files:**
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/qmodel_tq_forward.h`
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/qmodel_tq_forward.c` (partial — alloc/free only)
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_qmodel_tq.c` (partial — alloc test)

- [ ] **Step 1: Create qmodel_tq_forward.h**

```c
/* qmodel_tq_forward.h — Quantized weights + TurboQuant compressed KV cache
 *
 * Combines qmatmul_vec (on-the-fly weight dequant from llama_quant.c) with
 * TurboQuant compressed KV cache (tq_compress_key/val + tq_dot_key).
 * This is the forward pass for Process B on seL4 with real KV compression.
 *
 * Memory budget (Llama 3.2 1B, d=64):
 *   F32 KV cache: 16 layers × 512 seq × 8 heads × 64 dim × 4 bytes × 2 = 64 MB
 *   TQ KV cache:  16 layers × 512 seq × 8 heads × (sizeof(tq_ckey_t) + sizeof(tq_cval_t)) ≈ 8 MB
 *   Savings: ~56 MB (7.5x compression)
 *
 * NOTE: d=64 (Llama 3.2 1B) TQ quality is poor (8.9% token match).
 * d=128 (8B+) models are the intended operating regime for TurboQuant.
 */
#ifndef QMODEL_TQ_FORWARD_H
#define QMODEL_TQ_FORWARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "llama_model.h"
#include "llama_quant.h"
#include "turboquant.h"

typedef struct {
    /* Activation buffers (same as llama_state_t) */
    float *x;           /* Current token activations [dim] */
    float *xb;          /* Attention output buffer [dim] */
    float *xb2;         /* Scratch / FFN output buffer [dim] */
    float *q;           /* Query heads [n_heads * head_dim] */
    float *k;           /* Key heads [n_kv_heads * head_dim] */
    float *v;           /* Value heads [n_kv_heads * head_dim] */
    float *att;         /* Attention scores [n_heads * max_seq] */
    float *hb;          /* Hidden layer buffer [hidden_dim] */
    float *hb2;         /* Hidden layer buffer 2 [hidden_dim] */
    float *logits;      /* Output logits [vocab_size] */

    /* Compressed KV cache: indexed as [L * max_seq * n_kv_heads + pos * n_kv_heads + h] */
    tq_ckey_t *key_cache;   /* n_layers * max_seq * n_kv_heads entries */
    tq_cval_t *val_cache;   /* n_layers * max_seq * n_kv_heads entries */

    /* Per-layer TQ state (dynamically allocated, NOT fixed array) */
    tq_state_t *tq_states;  /* calloc(n_layers, sizeof(tq_state_t)) */

    int pos;
    int max_seq_len;
    int n_layers;
    int n_kv_heads;
    int key_bits;       /* TQ key bits used (for reporting) */
    int val_bits;       /* TQ val bits used (for reporting) */
} qmodel_tq_state_t;

/* Allocate state with TQ compressed KV cache.
 * key_bits/val_bits: 2-4 (uniform) or use tq_init_mixed via _mixed variant.
 * Returns 0 on success, -1 on error. */
int  qmodel_tq_alloc_state(qmodel_tq_state_t *state, const llama_config_t *config,
                            int key_bits, int val_bits, uint64_t base_seed);

/* Free all buffers and TQ states. */
void qmodel_tq_free_state(qmodel_tq_state_t *state);

/* One-token forward pass: quantized weight matmul + TQ compressed KV cache. */
void qmodel_tq_forward(const qmodel_t *qm, qmodel_tq_state_t *state, int token);

/* Prefill + autoregressive generation. Same interface as qmodel_generate. */
int  qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                         const int *prompt_tokens, int n_prompt,
                         int *output_tokens, int max_output,
                         int eos_token, float temperature, int top_k,
                         uint64_t seed);

/* Report memory usage comparison. */
void qmodel_tq_print_memory(const qmodel_tq_state_t *state, const llama_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* QMODEL_TQ_FORWARD_H */
```

- [ ] **Step 2: Write the test first**

Create `test_qmodel_tq.c`:

```c
/* test_qmodel_tq.c — Tests for quantized weights + TQ compressed KV cache */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "qmodel_tq_forward.h"

static int test_alloc_free(void) {
    printf("  test_alloc_free...");

    llama_config_t config = {
        .dim = 64,
        .n_layers = 4,
        .n_heads = 8,
        .n_kv_heads = 8,
        .head_dim = 8,
        .hidden_dim = 128,
        .vocab_size = 100,
        .max_seq_len = 32,
        .rope_theta = 10000.0f
    };

    qmodel_tq_state_t state;
    int rc = qmodel_tq_alloc_state(&state, &config, 3, 3, 42);
    if (rc != 0) { printf(" FAIL (alloc returned %d)\n", rc); return 1; }

    /* Verify fields */
    if (state.n_layers != 4) { printf(" FAIL (n_layers)\n"); return 1; }
    if (state.n_kv_heads != 8) { printf(" FAIL (n_kv_heads)\n"); return 1; }
    if (state.max_seq_len != 32) { printf(" FAIL (max_seq)\n"); return 1; }
    if (state.pos != 0) { printf(" FAIL (pos)\n"); return 1; }
    if (!state.key_cache) { printf(" FAIL (key_cache NULL)\n"); return 1; }
    if (!state.val_cache) { printf(" FAIL (val_cache NULL)\n"); return 1; }
    if (!state.tq_states) { printf(" FAIL (tq_states NULL)\n"); return 1; }
    if (!state.x || !state.logits) { printf(" FAIL (buffers NULL)\n"); return 1; }

    /* Verify TQ states initialized */
    if (state.tq_states[0].d != 8) { printf(" FAIL (tq d=%d)\n", state.tq_states[0].d); return 1; }
    if (state.tq_states[0].key_bits != 3) { printf(" FAIL (tq key_bits)\n"); return 1; }

    qmodel_tq_free_state(&state);

    /* Verify cleanup */
    if (state.key_cache != NULL) { printf(" FAIL (not cleaned)\n"); return 1; }

    printf(" PASS\n");
    return 0;
}

static int test_memory_savings(void) {
    printf("  test_memory_savings...");

    /* Llama 3.2 1B dimensions */
    llama_config_t config = {
        .dim = 2048,
        .n_layers = 16,
        .n_heads = 32,
        .n_kv_heads = 8,
        .head_dim = 64,
        .hidden_dim = 8192,
        .vocab_size = 128256,
        .max_seq_len = 512,
        .rope_theta = 500000.0f
    };

    qmodel_tq_state_t state;
    int rc = qmodel_tq_alloc_state(&state, &config, 3, 3, 42);
    if (rc != 0) { printf(" FAIL (alloc)\n"); return 1; }

    /* F32 KV cache would be: 16 * 512 * 8 * 64 * 4 * 2 = 64 MB */
    size_t f32_kv = (size_t)16 * 512 * 8 * 64 * sizeof(float) * 2;

    /* TQ KV cache: 16 * 512 * 8 * (sizeof(tq_ckey_t) + sizeof(tq_cval_t)) */
    size_t tq_kv = (size_t)16 * 512 * 8 * (sizeof(tq_ckey_t) + sizeof(tq_cval_t));

    float ratio = (float)f32_kv / (float)tq_kv;

    qmodel_tq_free_state(&state);

    if (ratio < 3.0f) {
        printf(" FAIL (ratio=%.1fx, expected >3x)\n", ratio);
        return 1;
    }

    printf(" PASS (F32=%zuMB, TQ=%zuMB, %.1fx compression)\n",
           f32_kv / (1024*1024), tq_kv / (1024*1024), ratio);
    return 0;
}

int main(void) {
    printf("=== qmodel_tq_forward tests ===\n");
    int fails = 0;
    fails += test_alloc_free();
    fails += test_memory_savings();
    printf("\n=== Results: %d PASS, %d FAIL ===\n", 2 - fails, fails);
    return fails;
}
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai
gcc -Wall -Werror -O2 -std=c11 -I. \
  test_qmodel_tq.c qmodel_tq_forward.c llama_quant.c dequant.c \
  tensor_ops.c sampling.c gguf_parser.c tokenizer.c turboquant.c \
  llama_load.c -lm -o /tmp/test_qmodel_tq
```

Expected: FAIL — `qmodel_tq_forward.c` doesn't exist yet.

- [ ] **Step 4: Implement alloc/free in qmodel_tq_forward.c**

```c
/* qmodel_tq_forward.c — Quantized weights + TurboQuant compressed KV cache */
#include "qmodel_tq_forward.h"
#include "qmatmul.h"
#include "dequant.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define RMS_EPS 1e-5f

int qmodel_tq_alloc_state(qmodel_tq_state_t *state, const llama_config_t *config,
                           int key_bits, int val_bits, uint64_t base_seed) {
    if (!state || !config) return -1;

    memset(state, 0, sizeof(*state));

    int dim = config->dim;
    int n_layers = config->n_layers;
    int n_heads = config->n_heads;
    int n_kv_heads = config->n_kv_heads;
    int head_dim = config->head_dim;
    int hidden_dim = config->hidden_dim;
    int vocab_size = config->vocab_size;
    int max_seq = config->max_seq_len;

    state->n_layers = n_layers;
    state->n_kv_heads = n_kv_heads;
    state->max_seq_len = max_seq;
    state->pos = 0;
    state->key_bits = key_bits;
    state->val_bits = val_bits;

    /* Activation buffers */
    state->x      = (float *)calloc(dim, sizeof(float));
    state->xb     = (float *)calloc(dim, sizeof(float));
    state->xb2    = (float *)calloc(dim, sizeof(float));
    state->q      = (float *)calloc(n_heads * head_dim, sizeof(float));
    state->k      = (float *)calloc(n_kv_heads * head_dim, sizeof(float));
    state->v      = (float *)calloc(n_kv_heads * head_dim, sizeof(float));
    state->att    = (float *)calloc(n_heads * max_seq, sizeof(float));
    state->hb     = (float *)calloc(hidden_dim, sizeof(float));
    state->hb2    = (float *)calloc(hidden_dim, sizeof(float));
    state->logits = (float *)calloc(vocab_size, sizeof(float));

    if (!state->x || !state->xb || !state->xb2 || !state->q || !state->k ||
        !state->v || !state->att || !state->hb || !state->hb2 || !state->logits) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* Compressed KV cache */
    size_t kv_entries = (size_t)n_layers * max_seq * n_kv_heads;
    state->key_cache = (tq_ckey_t *)calloc(kv_entries, sizeof(tq_ckey_t));
    state->val_cache = (tq_cval_t *)calloc(kv_entries, sizeof(tq_cval_t));
    if (!state->key_cache || !state->val_cache) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* Per-layer TQ states (dynamically allocated) */
    state->tq_states = (tq_state_t *)calloc(n_layers, sizeof(tq_state_t));
    if (!state->tq_states) {
        qmodel_tq_free_state(state);
        return -1;
    }
    for (int L = 0; L < n_layers; L++) {
        if (tq_init(&state->tq_states[L], head_dim, key_bits, val_bits, base_seed + L) != 0) {
            qmodel_tq_free_state(state);
            return -1;
        }
    }

    return 0;
}

void qmodel_tq_free_state(qmodel_tq_state_t *state) {
    if (!state) return;
    free(state->x);       free(state->xb);     free(state->xb2);
    free(state->q);       free(state->k);      free(state->v);
    free(state->att);     free(state->hb);     free(state->hb2);
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

void qmodel_tq_print_memory(const qmodel_tq_state_t *state, const llama_config_t *config) {
    size_t f32_kv = (size_t)config->n_layers * config->max_seq_len * config->n_kv_heads
                    * config->head_dim * sizeof(float) * 2;
    size_t tq_kv = (size_t)config->n_layers * config->max_seq_len * config->n_kv_heads
                   * (sizeof(tq_ckey_t) + sizeof(tq_cval_t));
    printf("[TQ] KV cache: F32=%zu KB, TQ=%zu KB, %.1fx compression\n",
           f32_kv / 1024, tq_kv / 1024, (float)f32_kv / (float)tq_kv);
    (void)state;
}

/* Forward pass and generate stubs — implemented in Task 3 */
void qmodel_tq_forward(const qmodel_t *qm, qmodel_tq_state_t *state, int token) {
    (void)qm; (void)state; (void)token;
    /* TODO: Task 3 */
}

int qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k,
                        uint64_t seed) {
    (void)qm; (void)state; (void)prompt_tokens; (void)n_prompt;
    (void)output_tokens; (void)max_output; (void)eos_token;
    (void)temperature; (void)top_k; (void)seed;
    return 0; /* TODO: Task 3 */
}
```

- [ ] **Step 5: Build and run test**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai
gcc -Wall -Werror -O2 -std=c11 -I. \
  test_qmodel_tq.c qmodel_tq_forward.c llama_quant.c dequant.c \
  tensor_ops.c sampling.c gguf_parser.c tokenizer.c turboquant.c \
  llama_load.c -lm -o /tmp/test_qmodel_tq && /tmp/test_qmodel_tq
```

Expected: 2 PASS, 0 FAIL.

- [ ] **Step 6: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/qmodel_tq_forward.h phase3/src/ai/qmodel_tq_forward.c \
        phase3/src/ai/test_qmodel_tq.c
git commit -m "feat: qmodel_tq_state_t with compressed KV cache alloc/free

New qmodel_tq_forward.h/c combining quantized weight matmul with
TurboQuant compressed KV cache. This commit: alloc/free + memory
reporting. Forward pass is stub (next commit).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Implement qmodel_tq_forward() and qmodel_tq_generate()

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/qmodel_tq_forward.c` (replace stubs)
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_qmodel_tq.c` (add forward pass test)

This is the core task. The forward pass is structurally identical to `qmodel_forward()` (llama_quant.c lines 278-437) but replaces the F32 KV cache store/retrieve with TQ compress/decompress.

- [ ] **Step 1: Add test for forward pass (requires synthetic model — just test it doesn't crash)**

Add to `test_qmodel_tq.c` before `main()`:

```c
static int test_forward_no_crash(void) {
    printf("  test_forward_no_crash...");

    /* Tiny synthetic config */
    llama_config_t config = {
        .dim = 16, .n_layers = 1, .n_heads = 2, .n_kv_heads = 2,
        .head_dim = 8, .hidden_dim = 32, .vocab_size = 10,
        .max_seq_len = 8, .rope_theta = 10000.0f
    };

    /* Create a fake qmodel_t with F32 weights (simplest) */
    qmodel_t qm;
    memset(&qm, 0, sizeof(qm));
    qm.config = config;

    /* Allocate fake F32 tensors as qtensor_t */
    size_t embed_bytes = 10 * 16 * sizeof(float);
    float *embed_data = (float *)calloc(1, embed_bytes);
    /* Put some nonzero values so we get nonzero activations */
    for (int i = 0; i < 10 * 16; i++) embed_data[i] = 0.01f * (i % 7 - 3);

    qm.token_embed = (qtensor_t){ .data = embed_data, .type = 0 /* F32 */, .n_elements = 10*16, .n_bytes = embed_bytes };
    qm.output_norm = (qtensor_t){ .data = calloc(16, sizeof(float)), .type = 0, .n_elements = 16, .n_bytes = 16*sizeof(float) };
    /* Set norm weights to 1.0 */
    { float *p = (float *)qm.output_norm.data; for (int i = 0; i < 16; i++) p[i] = 1.0f; }
    qm.output_weight = qm.token_embed; /* reuse for output projection */
    qm.rope_freqs = NULL;

    /* One layer with F32 weights */
    qlayer_t layer;
    memset(&layer, 0, sizeof(layer));
    size_t wq_bytes = 16 * 16 * sizeof(float);
    float *wq_data = (float *)calloc(1, wq_bytes);
    for (int i = 0; i < 16*16; i++) wq_data[i] = (i == (i/16)*16 + (i%16)) ? 0.1f : 0.0f; /* ~identity */
    layer.attn_norm = (qtensor_t){ .data = calloc(16, sizeof(float)), .type = 0, .n_elements = 16, .n_bytes = 16*sizeof(float) };
    { float *p = (float *)layer.attn_norm.data; for (int i = 0; i < 16; i++) p[i] = 1.0f; }
    layer.wq = (qtensor_t){ .data = wq_data, .type = 0, .n_elements = 16*16, .n_bytes = wq_bytes };
    layer.wk = layer.wq;
    layer.wv = layer.wq;
    layer.wo = layer.wq;
    layer.ffn_norm = layer.attn_norm;
    size_t wg_bytes = 32 * 16 * sizeof(float);
    float *wg_data = (float *)calloc(1, wg_bytes);
    for (int i = 0; i < 32*16; i++) wg_data[i] = 0.01f;
    layer.w_gate = (qtensor_t){ .data = wg_data, .type = 0, .n_elements = 32*16, .n_bytes = wg_bytes };
    layer.w_up = layer.w_gate;
    size_t wd_bytes = 16 * 32 * sizeof(float);
    float *wd_data = (float *)calloc(1, wd_bytes);
    for (int i = 0; i < 16*32; i++) wd_data[i] = 0.01f;
    layer.w_down = (qtensor_t){ .data = wd_data, .type = 0, .n_elements = 16*32, .n_bytes = wd_bytes };

    qm.layers = &layer;
    qm.loaded = true;

    /* Allocate TQ state */
    qmodel_tq_state_t state;
    int rc = qmodel_tq_alloc_state(&state, &config, 3, 3, 42);
    if (rc != 0) { printf(" FAIL (alloc)\n"); return 1; }

    /* Run forward pass on token 1 — should not crash */
    qmodel_tq_forward(&qm, &state, 1);

    /* Logits should be non-zero (not all zeros) */
    float sum = 0;
    for (int i = 0; i < 10; i++) sum += fabsf(state.logits[i]);
    if (sum < 1e-10f) { printf(" FAIL (all-zero logits)\n"); qmodel_tq_free_state(&state); return 1; }

    /* Run a second token to test KV cache works */
    qmodel_tq_forward(&qm, &state, 2);
    if (state.pos != 2) { printf(" FAIL (pos=%d)\n", state.pos); qmodel_tq_free_state(&state); return 1; }

    qmodel_tq_free_state(&state);
    free(embed_data); free((void*)qm.output_norm.data); free(wq_data); free(wg_data); free(wd_data);
    free((void*)layer.attn_norm.data);

    printf(" PASS\n");
    return 0;
}
```

Add `fails += test_forward_no_crash();` to main, update the pass count.

- [ ] **Step 2: Implement qmodel_tq_forward()**

Replace the stub in `qmodel_tq_forward.c` with the full implementation. The structure follows `qmodel_forward()` (llama_quant.c:278-437) exactly, with these changes in the KV cache section:

**Lines that CHANGE from qmodel_forward (F32 cache) to qmodel_tq_forward (TQ cache):**

**Cache store (replaces memcpy at llama_quant.c:354-358):**
```c
    /* Compress K,V into TQ cache */
    int cache_base = (L * max_seq + pos) * n_kv_heads;
    for (int h = 0; h < n_kv_heads; h++) {
        tq_compress_key(&state->tq_states[L], state->k + h * head_dim,
                        &state->key_cache[cache_base + h]);
        tq_compress_val(&state->tq_states[L], state->v + h * head_dim,
                        &state->val_cache[cache_base + h]);
    }
```

**Attention scoring (replaces dot product at llama_quant.c:367-372):**
```c
    /* QJL-corrected attention scoring */
    for (int t = 0; t <= pos; t++) {
        int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
        float score = tq_dot_key(&state->tq_states[L], q_head,
                                  &state->key_cache[t_idx]);
        att_h[t] = score / sqrtf((float)head_dim);
    }
```

**Weighted value sum (replaces direct cache access at llama_quant.c:379-386):**
```c
    /* On-the-fly value decompression */
    float tmp_val[TQ_MAX_HEAD_DIM];
    for (int t = 0; t <= pos; t++) {
        int t_idx = (L * max_seq + t) * n_kv_heads + kv_h;
        tq_decompress_val(&state->tq_states[L], &state->val_cache[t_idx], tmp_val);
        float w = att_h[t];
        for (int d = 0; d < head_dim; d++)
            out_h[d] += w * tmp_val[d];
    }
```

Everything else (embedding lookup, RMS norm, qmatmul_vec projections, RoPE, SwiGLU, residual adds, final norm, output projection) is copied from `qmodel_forward()` verbatim but calls `qmatmul_vec()` and `qembed_lookup()` via the shared `qmatmul.h` header.

- [ ] **Step 3: Implement qmodel_tq_generate()**

Same structure as `qmodel_generate()` (llama_quant.c:441-470) but calls `qmodel_tq_forward()` instead of `qmodel_forward()`:

```c
int qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k,
                        uint64_t seed) {
    /* Prefill: process all prompt tokens */
    for (int i = 0; i < n_prompt; i++)
        qmodel_tq_forward(qm, state, prompt_tokens[i]);

    /* Autoregressive generation */
    int count = 0;
    for (int i = 0; i < max_output; i++) {
        int next;
        if (temperature <= 0.0f || top_k <= 1)
            next = sample_greedy(state->logits, qm->config.vocab_size);
        else
            next = sample_topk(state->logits, qm->config.vocab_size,
                              top_k, temperature, &seed);

        if (next == eos_token) break;
        output_tokens[count++] = next;
        qmodel_tq_forward(qm, state, next);
    }
    return count;
}
```

- [ ] **Step 4: Build and run tests**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai
gcc -Wall -Werror -O2 -std=c11 -I. \
  test_qmodel_tq.c qmodel_tq_forward.c llama_quant.c dequant.c \
  tensor_ops.c sampling.c gguf_parser.c tokenizer.c turboquant.c \
  llama_load.c -lm -o /tmp/test_qmodel_tq && /tmp/test_qmodel_tq
```

Expected: 3 PASS, 0 FAIL (alloc_free + memory_savings + forward_no_crash).

- [ ] **Step 5: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/qmodel_tq_forward.c phase3/src/ai/test_qmodel_tq.c
git commit -m "feat: qmodel_tq_forward — quantized weights + TQ compressed KV cache

Combined forward pass: qmatmul_vec for weight projections (on-the-fly
dequant), tq_compress_key/val for KV storage, tq_dot_key for QJL-
corrected attention scoring, tq_decompress_val for value retrieval.
F32 KV cache (64MB) → TQ KV cache (~8MB) = ~8x compression.

NOTE: d=64 (1B) quality is poor. Real payoff is d=128 on spare PC.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Add MSG_SET_TQ_MODE and Wire into Inference Server

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ipc/shmem_ipc.h` (add message type)
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/sel4/inference_server.c` (add TQ handler)

- [ ] **Step 1: Add MSG_SET_TQ_MODE to shmem_ipc.h**

After `MSG_STATE_ACK 0x0E` (line ~34), add:

```c
#define MSG_SET_TQ_MODE    0x0F  /* Enable/disable TurboQuant: payload[0]=1 enable, 0 disable */
```

- [ ] **Step 2: Read inference_server.c and plan the changes**

Read the full file. The key changes:
1. Add `#include "qmodel_tq_forward.h"` to includes
2. Add `qmodel_tq_state_t tq_state;` and `bool tq_enabled = false;` alongside the existing `llama_state_t state;`
3. Allocate TQ state after model loading (alongside existing `llama_alloc_state`)
4. Add a `handle_query_tq()` function (or branch in `handle_query`) that uses `qmodel_tq_generate()` instead of `qmodel_generate()`
5. Handle `MSG_SET_TQ_MODE` in the switch statement

- [ ] **Step 3: Modify inference_server.c includes**

After `#include "sampling.h"` (line ~29), add:

```c
#include "qmodel_tq_forward.h"
```

- [ ] **Step 4: Add TQ state allocation after model loading**

After `llama_alloc_state(&state, &qm.config)` (around line 200), add:

```c
    /* Allocate TurboQuant compressed KV cache state */
    qmodel_tq_state_t tq_state;
    bool tq_enabled = false;
    if (qmodel_tq_alloc_state(&tq_state, &qm.config, 3, 3, 42) == 0) {
        puts_serial("[Process B] TQ state allocated (");
        qmodel_tq_print_memory(&tq_state, &qm.config);
        puts_serial(")\n");
    } else {
        puts_serial("[Process B] WARNING: TQ state allocation failed\n");
    }
```

- [ ] **Step 5: Add handle_query_tq function**

Before `handle_query()`, add a TQ variant that calls `qmodel_tq_generate()`:

```c
#ifdef JARVIS_HAS_MODEL
static void handle_query_tq(shmem_ring_t *response_ring, seL4_CPtr resp_notif,
                             uint16_t seq, const char *query, uint16_t query_len,
                             qmodel_t *qm, qmodel_tq_state_t *state, tokenizer_t *tok,
                             int bos_id) {
    char query_buf[241];
    int copy_len = (query_len < 240) ? query_len : 240;
    memcpy(query_buf, query, copy_len);
    query_buf[copy_len] = '\0';

    int prompt_ids[64];
    prompt_ids[0] = bos_id;
    int n_extra = tokenizer_encode(tok, query_buf, prompt_ids + 1, 63);
    int n_prompt = 1 + (n_extra > 0 ? n_extra : 0);

    /* Reset TQ state */
    state->pos = 0;
    memset(state->key_cache, 0,
           (size_t)state->n_layers * state->max_seq_len * state->n_kv_heads * sizeof(tq_ckey_t));
    memset(state->val_cache, 0,
           (size_t)state->n_layers * state->max_seq_len * state->n_kv_heads * sizeof(tq_cval_t));

    int output_ids[64];
    int n_out = qmodel_tq_generate(qm, state, prompt_ids, n_prompt,
                                    output_ids, 50, tok->eos_id, 0.0f, 1, 42);

    char response[512];
    int rlen = tokenizer_decode(tok, output_ids, n_out, response, (int)sizeof(response) - 1);
    if (rlen < 0) rlen = 0;
    response[rlen] = '\0';

    /* Send response (split if needed) */
    int offset = 0;
    while (offset < rlen) {
        int chunk = (rlen - offset > SHMEM_MAX_PAYLOAD) ? SHMEM_MAX_PAYLOAD : (rlen - offset);
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq,
                       response + offset, (uint16_t)chunk);
        offset += chunk;
    }
    if (rlen == 0) {
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq, "[TQ: no output]", 15);
    }
    seL4_Signal(resp_notif);
}
#endif
```

- [ ] **Step 6: Add MSG_SET_TQ_MODE handler in the IPC switch**

In the main IPC loop switch statement (around line 235), add:

```c
        case MSG_SET_TQ_MODE: {
            tq_enabled = (msg_len > 0 && payload[0] == 1);
            puts_serial("[Process B] TQ mode: ");
            puts_serial(tq_enabled ? "ENABLED\n" : "DISABLED\n");
            uint8_t ack = tq_enabled ? 1 : 0;
            shmem_ipc_send(response_ring, MSG_STATE_ACK, msg_seq, &ack, 1);
            seL4_Signal(resp_notif);
            break;
        }
```

- [ ] **Step 7: Branch on tq_enabled in MSG_QUERY handler**

Change the existing `case MSG_QUERY:` from:
```c
        case MSG_QUERY:
            handle_query(response_ring, resp_notif, msg_seq, (const char*)payload, msg_len,
                        &qm, &state, &tok, vocab.bos_id);
            break;
```

to:
```c
        case MSG_QUERY:
            if (tq_enabled) {
                handle_query_tq(response_ring, resp_notif, msg_seq,
                               (const char*)payload, msg_len,
                               &qm, &tq_state, &tok, vocab.bos_id);
            } else {
                handle_query(response_ring, resp_notif, msg_seq,
                            (const char*)payload, msg_len,
                            &qm, &state, &tok, vocab.bos_id);
            }
            break;
```

- [ ] **Step 8: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ipc/shmem_ipc.h phase3/src/sel4/inference_server.c
git commit -m "feat: MSG_SET_TQ_MODE (0x0F) runtime toggle for TQ KV compression

Process B now supports runtime switching between standard F32 KV cache
and TurboQuant compressed KV cache via MSG_SET_TQ_MODE IPC message.
Default: standard path (backward compatible).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Add Comparison Demo to Process A (main_x86.c)

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/sel4/main_x86.c`

- [ ] **Step 1: Read the existing test query section (around line 832)**

Find the `"Sending test query to Process B"` section.

- [ ] **Step 2: Add comparison demo after the existing test query**

After the existing test query response is printed, add:

```c
    /* ---- TurboQuant A/B Comparison Demo ---- */
    puts_serial("\n[JARVIS] === TurboQuant A/B Comparison ===\n");

    /* Standard query (already done above, do it again for clean comparison) */
    puts_serial("[JARVIS] [Standard] Query: \"The seL4 microkernel is\"\n");
    {
        const char *q2 = "The seL4 microkernel is";
        state.pos = 0;  /* This is Process A's local state — not relevant here */

        shmem_ipc_send(shared_request_ring, MSG_QUERY, 10,
                       q2, (uint16_t)strlen(q2));
        seL4_Signal(req_notif);
        seL4_Wait(resp_notif, NULL);

        uint8_t rt; uint16_t rs; uint8_t rp[SHMEM_MAX_PAYLOAD]; uint16_t rl;
        if (shmem_ipc_recv(shared_response_ring, &rt, &rs, rp, &rl) == 0) {
            rp[rl] = '\0';
            puts_serial("[JARVIS] [Standard] Response: \"");
            puts_serial((const char *)rp);
            puts_serial("\"\n");
        }
    }

    /* Enable TQ mode */
    puts_serial("[JARVIS] Enabling TurboQuant mode...\n");
    {
        uint8_t enable = 1;
        shmem_ipc_send(shared_request_ring, MSG_SET_TQ_MODE, 20, &enable, 1);
        seL4_Signal(req_notif);
        seL4_Wait(resp_notif, NULL);
        /* Drain ACK */
        uint8_t rt; uint16_t rs; uint8_t rp[SHMEM_MAX_PAYLOAD]; uint16_t rl;
        shmem_ipc_recv(shared_response_ring, &rt, &rs, rp, &rl);
    }

    /* TQ query (same prompt) */
    puts_serial("[JARVIS] [TurboQuant] Query: \"The seL4 microkernel is\"\n");
    {
        const char *q3 = "The seL4 microkernel is";
        shmem_ipc_send(shared_request_ring, MSG_QUERY, 11,
                       q3, (uint16_t)strlen(q3));
        seL4_Signal(req_notif);
        seL4_Wait(resp_notif, NULL);

        uint8_t rt; uint16_t rs; uint8_t rp[SHMEM_MAX_PAYLOAD]; uint16_t rl;
        if (shmem_ipc_recv(shared_response_ring, &rt, &rs, rp, &rl) == 0) {
            rp[rl] = '\0';
            puts_serial("[JARVIS] [TurboQuant] Response: \"");
            puts_serial((const char *)rp);
            puts_serial("\"\n");
        }
    }

    /* Disable TQ mode (return to standard) */
    {
        uint8_t disable = 0;
        shmem_ipc_send(shared_request_ring, MSG_SET_TQ_MODE, 21, &disable, 1);
        seL4_Signal(req_notif);
        seL4_Wait(resp_notif, NULL);
        uint8_t rt; uint16_t rs; uint8_t rp[SHMEM_MAX_PAYLOAD]; uint16_t rl;
        shmem_ipc_recv(shared_response_ring, &rt, &rs, rp, &rl);
    }

    puts_serial("[JARVIS] === A/B Comparison Complete ===\n\n");
```

- [ ] **Step 3: Add `#include "shmem_ipc.h"` if not already present**

Check includes — `shmem_ipc.h` should already be included (line ~32). Verify MSG_SET_TQ_MODE is visible.

- [ ] **Step 4: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/sel4/main_x86.c
git commit -m "feat: A/B comparison demo — standard vs TurboQuant KV cache

Process A sends same query twice: first with standard F32 KV cache,
then with TurboQuant compressed cache (via MSG_SET_TQ_MODE toggle).
Both responses printed side-by-side for quality comparison.

NOTE: d=64 (1B) quality will be poor — this is expected and documented.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Add CI Step and Update CLAUDE.md

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/.github/workflows/ci.yml`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/CLAUDE.md`

- [ ] **Step 1: Add CI step for test_qmodel_tq.c**

After the existing TurboQuant CI steps (around line 391), add:

```yaml
    - name: "Phase 3: Quantized TQ Forward Pass (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 \
          -I phase3/src/ai \
          phase3/src/ai/test_qmodel_tq.c \
          phase3/src/ai/qmodel_tq_forward.c \
          phase3/src/ai/llama_quant.c \
          phase3/src/ai/dequant.c \
          phase3/src/ai/tensor_ops.c \
          phase3/src/ai/sampling.c \
          phase3/src/ai/gguf_parser.c \
          phase3/src/ai/tokenizer.c \
          phase3/src/ai/turboquant.c \
          phase3/src/ai/llama_load.c \
          -lm -o /tmp/test_qmodel_tq
        /tmp/test_qmodel_tq
```

- [ ] **Step 2: Verify CI command works locally**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
gcc -Wall -Werror -O2 -std=c11 \
  -I phase3/src/ai \
  phase3/src/ai/test_qmodel_tq.c \
  phase3/src/ai/qmodel_tq_forward.c \
  phase3/src/ai/llama_quant.c \
  phase3/src/ai/dequant.c \
  phase3/src/ai/tensor_ops.c \
  phase3/src/ai/sampling.c \
  phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c \
  phase3/src/ai/turboquant.c \
  phase3/src/ai/llama_load.c \
  -lm -o /tmp/test_qmodel_tq && /tmp/test_qmodel_tq
```

Expected: All PASS.

- [ ] **Step 3: Update CLAUDE.md**

Add to Phase 3 status table (after TurboQuant × llama.cpp benchmark entry):
```
| **qmodel_tq_forward: quantized weights + TQ compressed KV cache** | **DONE** |
| **MSG_SET_TQ_MODE: runtime TQ toggle in Process B** | **DONE** |
| **A/B comparison demo in Process A (standard vs TQ)** | **DONE** |
```

Add to Phase 3 Early Work table:
```
| Quantized TQ forward pass | — | DONE | qmodel_tq_forward.c/h, qmatmul.h | 3+ PASS |
```

Add to Quick Reference:
```
- **Quantized TQ Forward:** `phase3/src/ai/qmodel_tq_forward.c/h`
- **Shared Matmul Header:** `phase3/src/ai/qmatmul.h`
```

Update test count: current + 3 (test_alloc_free, test_memory_savings, test_forward_no_crash).

- [ ] **Step 4: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add .github/workflows/ci.yml CLAUDE.md
git commit -m "docs: CI step for qmodel_tq tests, update CLAUDE.md status

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 5: Push and check CI**

```bash
git push
GH_TOKEN=$(git remote get-url origin | grep -oP '(?<=:)ghp_[^@]+') gh run list --limit 1 -R ITSYABOI119/JARVIS_OS
```

---

## Critical Reminders

1. **qmatmul_vec and qembed_lookup are currently static.** Task 1 MUST complete before Task 3 can compile. The functions go from `static` to non-static in llama_quant.c and get declarations in qmatmul.h.

2. **tq_states is dynamically allocated.** Use `calloc(n_layers, sizeof(tq_state_t))`, NOT a fixed array like `tq_state_t tq_states[TQ_MAX_LAYERS]`. This avoids a 128-entry × ~33KB = 4MB stack allocation.

3. **d=64 quality is poor.** Llama 3.2 1B has head_dim=64. TurboQuant at d=64 gives 8.9% token match. This is expected and documented. The code works mechanically — the output just degrades. Real payoff is d=128 models on the spare PC.

4. **Memory budget: 4GB QEMU, 128MB morecore.** The TQ state uses ~8MB for KV cache (vs 64MB for F32). The per-layer TQ matrices (Pi, S) add ~33KB × 16 layers = 528KB. Total TQ overhead: ~9MB vs 64MB = saves 55MB. This is significant in the 128MB morecore budget.

5. **Don't modify qmodel_forward or llama_tq_forward.** These are existing, tested code paths. Create NEW files only (qmodel_tq_forward.c/h, qmatmul.h).

6. **MSG_SET_TQ_MODE default is OFF.** Process B starts with standard path. The toggle must be explicitly sent by Process A.

7. **RoPE rope_freqs handling.** Both `qmodel_forward` and `llama_tq_forward` handle `rope_freqs` (custom frequency divisors). The new forward pass must also check `qm->rope_freqs` and apply divisors if present (see llama_quant.c:336-345).

8. **CLAUDE.md rules.** Every commit that adds tests or files must update CLAUDE.md. Every new test file must get a CI step. Push and check `gh run list` after final commit.
