# Gemma 4 Engine Work Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Gemma 4 architecture support to the JARVIS C inference engine so Gemma 4 E2B (8.40/10 quality, #1 ranked model) runs natively on bare-metal seL4.

**Architecture:** Extend the existing `qmodel_forward()` production path with 7 features: architecture-aware metadata loading, logit softcapping, sandwich norms (post-attention + post-FFN RMSNorm), per-layer embeddings (PLE), per-head Q/K RMSNorm + dual RoPE, sliding-window attention, and shared KV cache. All features are gated by config fields that default to "off" for existing Llama models, ensuring zero regression. Work targets the `feature/gemma4-arch` branch.

**Tech Stack:** C11, no C++, no external deps. AVX2+FMA optional. Quantized zero-copy inference via `dequant.c`. Tests compile with `gcc -Wall -Werror -O2 -std=c11`.

**Branch:** `feature/gemma4-arch` (created from current `master` at commit `5281050`)

---

## Gemma 4 E2B Architecture Reference

These values are byte-verified from the actual GGUF file and from the llama.cpp reference implementation (build 8728, `src/models/gemma4-iswa.cpp`).

```
Metadata (gemma4.* GGUF keys):
  block_count                      = 35
  embedding_length                 = 1536
  attention.head_count             = 8
  attention.head_count_kv          = 1
  attention.key_length             = 512       (global layers head_dim)
  attention.key_length_swa         = 256       (SWA layers head_dim)
  attention.value_length           = 512
  attention.value_length_swa       = 256
  feed_forward_length              = arr[i32,35] (6144 for layers 0-14, 12288 for 15-34)
  rope.freq_base                   = 1000000.0 (global layers)
  rope.freq_base_swa               = 10000.0   (SWA layers)
  attention.sliding_window         = 512
  attention.shared_kv_layers       = 20        (last 20 of 35 layers share KV)
  attention.sliding_window_pattern = arr[bool,35] (per-layer: true=SWA, false=global)
  embedding_length_per_layer_input = 256       (PLE embedding dim)
  final_logit_softcapping          = 30.0
  attention.layer_norm_rms_epsilon = 1e-6
  rope.dimension_count             = 512       (global RoPE dim)
  rope.dimension_count_swa         = 256       (SWA RoPE dim)

GGUF Tensor Inventory (BYTE-VERIFIED from unsloth/gemma-4-E2B-it-GGUF Q4_K_M, 32MB header):

Global tensors (6):
  token_embd.weight                  (standard token embedding)
  output_norm.weight                 (final RMSNorm)
  output.weight                      (output projection — NOT tied)
  per_layer_token_embd.weight        (PLE embedding table: [vocab_size x ple_dim])
  per_layer_model_proj.weight        (PLE projection: [ple_dim x dim])
  per_layer_proj_norm.weight         (PLE projection norm: [ple_dim])

Per-layer tensors (17 per layer × 35 layers):
  --- Standard (9, same as Llama) ---
  blk.N.attn_norm.weight             (pre-attention RMSNorm)
  blk.N.attn_q.weight                (Q projection)
  blk.N.attn_k.weight                (K projection)
  blk.N.attn_v.weight                (V projection)
  blk.N.attn_output.weight           (attention output projection)
  blk.N.ffn_norm.weight              (pre-FFN RMSNorm)
  blk.N.ffn_gate.weight              (SwiGLU gate)
  blk.N.ffn_up.weight                (SwiGLU up)
  blk.N.ffn_down.weight              (SwiGLU down)
  --- New (8, Gemma 4 specific) ---
  blk.N.attn_q_norm.weight           (per-head Q RMSNorm, before RoPE)
  blk.N.attn_k_norm.weight           (per-head K RMSNorm, before RoPE)
  blk.N.post_attention_norm.weight   (post-attention RMSNorm — sandwich norm)
  blk.N.post_ffw_norm.weight         (post-FFN RMSNorm — sandwich norm)
  blk.N.inp_gate.weight              (PLE input gate, GELU-activated)
  blk.N.proj.weight                  (PLE per-layer projection)
  blk.N.layer_output_scale.weight    (learned layer output scaling factor)
  blk.N.post_norm.weight             (additional per-layer norm)

NOTE: The plan previously referenced tensor names from llama.cpp internal mappings.
These GGUF names are the actual strings found in the binary file:
  CORRECTED: "token_embd_per_layer" → "per_layer_token_embd"
  CORRECTED: "cross_attn_attn_gate" → "inp_gate"
  ADDED: post_attention_norm, post_ffw_norm, post_norm, layer_output_scale, proj

Shape notes:
  Global layers: wq=[n_heads*head_dim, dim]=[4096,1536], wk=[n_kv*head_dim, dim]=[512,1536]
  SWA layers:    wq=[n_heads*head_dim_swa, dim]=[2048,1536], wk=[n_kv*head_dim_swa, dim]=[256,1536]
  Decoupled: n_heads * head_dim =/= dim (8*512=4096, but dim=1536)

Shared KV logic (from llama.cpp):
  n_layer_kv_from_start = n_layers - shared_kv_layers = 35 - 20 = 15
  Layers 0-14: compute their own K/V (15 unique KV layers)
  Layers 15-34: reuse K/V from the last layer with matching attention type (SWA or global)
```

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `phase3/src/ai/llama_model.h` | Modify | Extend `llama_config_t` with Gemma 4 fields; add `LLAMA_MAX_LAYERS` constant |
| `phase3/src/ai/llama_load.c` | Modify | Arch-aware config loading; read `general.architecture`, dispatch `{arch}.*` keys; read per-layer arrays; explicit head_dim |
| `phase3/src/ai/llama_quant.h` | Modify | Extend `qlayer_t` with optional PLE gate + Q/K norm tensors; extend `qmodel_t` with PLE global tensors |
| `phase3/src/ai/llama_quant.c` | Modify | Extended `qmodel_load()` (PLE + Q/K norm tensors); extended `qmodel_forward()` (PLE, SWA, dual RoPE, shared KV, softcap) |
| `phase3/src/ai/gguf_parser.c` | Modify | Add `gguf_find_kv()` helper (returns raw kv_t pointer for array iteration) |
| `phase3/src/ai/test_gemma4_config.c` | Create | Unit tests for config loading, per-layer arrays, arch dispatch |
| `phase3/src/ai/test_gemma4_forward.c` | Create | Unit tests for each forward-pass feature (softcap, PLE, SWA mask, shared KV, dual RoPE) |
| `.github/workflows/ci.yml` | Modify | Add CI steps for both new test files |

---

## Task 1: Extend Config Struct + Arch-Aware Metadata Loading

**Files:**
- Modify: `phase3/src/ai/llama_model.h:18-28` (llama_config_t)
- Modify: `phase3/src/ai/llama_load.c:34-107` (llama_load_config)
- Modify: `phase3/src/ai/gguf_parser.c` (add find_kv helper)
- Create: `phase3/src/ai/test_gemma4_config.c`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Write failing test for arch-aware config loading**

```c
/* test_gemma4_config.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llama_model.h"
#include "gguf_parser.h"

static int pass = 0, fail = 0;
#define TEST(name) printf("  %s ... ", name)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { fail++; printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* Build a minimal in-memory GGUF with gemma4.* metadata keys.
 * Uses gguf_open_memory() to parse. */
static void build_gemma4_gguf(uint8_t *buf, size_t *len);

static void test_arch_dispatch_gemma4(void)
{
    TEST("arch dispatch reads gemma4.* keys");
    uint8_t gguf_buf[8192];
    size_t gguf_len = 0;
    build_gemma4_gguf(gguf_buf, &gguf_len);

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, gguf_buf, gguf_len);
    ASSERT(err == 0, "gguf_open_memory failed");

    llama_config_t config;
    err = llama_load_config(&config, &ctx);
    ASSERT(err == 0, "llama_load_config failed for gemma4");
    ASSERT(config.dim == 1536, "dim should be 1536");
    ASSERT(config.n_layers == 35, "n_layers should be 35");
    ASSERT(config.n_heads == 8, "n_heads should be 8");
    ASSERT(config.n_kv_heads == 1, "n_kv_heads should be 1");
    ASSERT(config.head_dim == 512, "head_dim should be 512 (explicit, not derived)");
    ASSERT(config.hidden_dim == 6144, "hidden_dim should be 6144 (first layer FFN)");
    ASSERT(config.rope_theta > 999000.0f, "rope_theta should be ~1000000");
    ASSERT(config.rms_norm_eps < 1e-5f, "rms_norm_eps should be 1e-6");
    ASSERT(config.logit_softcap > 29.0f, "logit_softcap should be 30.0");
    ASSERT(config.ple_dim == 256, "ple_dim should be 256");
    ASSERT(config.swa_window == 512, "swa_window should be 512");
    ASSERT(config.rope_theta_swa > 9000.0f, "rope_theta_swa should be 10000");
    ASSERT(config.shared_kv_layers == 20, "shared_kv_layers should be 20");
    ASSERT(config.head_dim_swa == 256, "head_dim_swa should be 256");

    gguf_close(&ctx);
    llama_free_config(&config);
    PASS();
}

static void test_llama_config_unchanged(void)
{
    TEST("existing llama.* config still works");
    /* Build a minimal GGUF with llama.* keys (same as existing models) */
    /* ... (build_llama_gguf helper) ... */
    /* Verify: dim, n_layers, head_dim derived, ple_dim==0, swa_window==0, etc. */
    PASS(); /* placeholder — full impl in step 3 */
}

int main(void)
{
    printf("=== Gemma 4 Config Tests ===\n\n");
    test_arch_dispatch_gemma4();
    test_llama_config_unchanged();
    printf("\n=== Results: %d PASS, %d FAIL ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run test — verify compile fails (new fields don't exist)**

Run: `gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai phase3/src/ai/test_gemma4_config.c phase3/src/ai/llama_load.c phase3/src/ai/gguf_parser.c phase3/src/ai/dequant.c -lm -o /tmp/test_gemma4_config`
Expected: compile error — `llama_config_t` has no field `rms_norm_eps`, `logit_softcap`, etc.

- [ ] **Step 3: Extend `llama_config_t` in `llama_model.h`**

Add after existing `rope_theta` field (line 27):

```c
typedef struct {
    int dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int head_dim;
    int hidden_dim;
    int vocab_size;
    int max_seq_len;
    float rope_theta;

    /* --- Gemma 4 extensions (all default to 0/NULL for Llama models) --- */
    float rms_norm_eps;       /* 0.0 = use default 1e-5 */
    float logit_softcap;      /* 0.0 = no softcapping */
    float rope_theta_swa;     /* 0.0 = no SWA layers */
    int   ple_dim;            /* 0   = no PLE */
    int   swa_window;         /* 0   = no sliding window */
    int   shared_kv_layers;   /* 0   = all layers compute own KV */
    int   head_dim_swa;       /* 0   = no SWA-specific head dim */

    /* Per-layer config (malloc'd, NULL for uniform/Llama models) */
    int  *layer_ffn_dim;      /* [n_layers] — per-layer FFN hidden dim */
    bool *layer_is_swa;       /* [n_layers] — true = sliding window attention */
    int  *kv_share_map;       /* [n_layers] — -1 = own KV, >=0 = index of source layer */
} llama_config_t;
```

Add cleanup function declaration:
```c
void llama_free_config(llama_config_t *config);
```

Add constant:
```c
#define LLAMA_MAX_LAYERS 128
```

- [ ] **Step 4: Implement arch-aware `llama_load_config()` in `llama_load.c`**

Replace the hardcoded `"llama.*"` key lookups with a dynamic arch prefix. Key changes:

```c
int llama_load_config(llama_config_t *config, const gguf_ctx_t *ctx)
{
    if (!config || !ctx) return -1;
    memset(config, 0, sizeof(*config));

    /* Determine architecture prefix: "llama", "gemma4", "qwen3", etc. */
    const char *arch = gguf_get_kv_string(ctx, "general.architecture");
    if (!arch) arch = "llama";  /* backward compat */

    char key[256];
    uint32_t u32_val;
    float f32_val;

    /* Read embedding dimension */
    snprintf(key, sizeof(key), "%s.embedding_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->dim = (int)u32_val;
    else
        return -1;

    /* Read number of layers */
    snprintf(key, sizeof(key), "%s.block_count", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->n_layers = (int)u32_val;
    else
        return -1;

    /* ... same pattern for all existing keys ... */

    /* Explicit head_dim (Gemma 4, Qwen3 — overrides derivation) */
    snprintf(key, sizeof(key), "%s.attention.key_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->head_dim = (int)u32_val;
    else
        config->head_dim = config->dim / config->n_heads;  /* Llama fallback */

    /* RMS norm epsilon (defaults to 1e-5 if absent) */
    snprintf(key, sizeof(key), "%s.attention.layer_norm_rms_epsilon", arch);
    if (gguf_get_kv_f32(ctx, key, &f32_val))
        config->rms_norm_eps = f32_val;
    /* else stays 0.0 — forward pass uses: eps > 0 ? eps : 1e-5f */

    /* Logit softcapping */
    snprintf(key, sizeof(key), "%s.final_logit_softcapping", arch);
    gguf_get_kv_f32(ctx, key, &config->logit_softcap);  /* 0.0 if absent */

    /* PLE dimension */
    snprintf(key, sizeof(key), "%s.embedding_length_per_layer_input", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->ple_dim = (int)u32_val;

    /* Sliding window */
    snprintf(key, sizeof(key), "%s.attention.sliding_window", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->swa_window = (int)u32_val;

    /* SWA RoPE theta */
    snprintf(key, sizeof(key), "%s.rope.freq_base_swa", arch);
    gguf_get_kv_f32(ctx, key, &config->rope_theta_swa);

    /* SWA head dim */
    snprintf(key, sizeof(key), "%s.attention.key_length_swa", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->head_dim_swa = (int)u32_val;

    /* Shared KV layers */
    snprintf(key, sizeof(key), "%s.attention.shared_kv_layers", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->shared_kv_layers = (int)u32_val;

    /* Build per-layer arrays if needed */
    if (config->n_layers > 0 && config->n_layers <= LLAMA_MAX_LAYERS) {
        /* SWA pattern: read bool array from GGUF or infer */
        /* KV share map: compute from shared_kv_layers */
        /* Per-layer FFN dim: read array or use uniform hidden_dim */
        /* ... (see Task 2 for the array-reading logic) ... */
    }

    return 0;
}

void llama_free_config(llama_config_t *config)
{
    if (!config) return;
    free(config->layer_ffn_dim);
    free(config->layer_is_swa);
    free(config->kv_share_map);
    config->layer_ffn_dim = NULL;
    config->layer_is_swa = NULL;
    config->kv_share_map = NULL;
}
```

- [ ] **Step 5: Implement `build_gemma4_gguf()` test helper + complete both tests**

The helper builds a minimal in-memory GGUF with `general.architecture = "gemma4"` and all the required `gemma4.*` scalar metadata keys. Also implement `build_llama_gguf()` for the regression test. Both use the binary GGUF format (header + KV pairs, no tensor data needed for config-only tests).

- [ ] **Step 6: Run tests — verify both pass**

Run: `gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai phase3/src/ai/test_gemma4_config.c phase3/src/ai/llama_load.c phase3/src/ai/gguf_parser.c phase3/src/ai/dequant.c -lm -o /tmp/test_gemma4_config && /tmp/test_gemma4_config`
Expected: 2 PASS, 0 FAIL

- [ ] **Step 7: Add CI step to `.github/workflows/ci.yml`**

```yaml
    - name: "Phase 3: Gemma 4 Config (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
          -I phase3/src/ai \
          phase3/src/ai/test_gemma4_config.c \
          phase3/src/ai/llama_load.c \
          phase3/src/ai/gguf_parser.c \
          phase3/src/ai/dequant.c \
          -lm -o /tmp/test_gemma4_config
        /tmp/test_gemma4_config
```

- [ ] **Step 8: Commit**

```bash
git add phase3/src/ai/llama_model.h phase3/src/ai/llama_load.c \
  phase3/src/ai/test_gemma4_config.c .github/workflows/ci.yml
git commit -m "feat: arch-aware metadata loading — read general.architecture, dispatch {arch}.* keys

Extends llama_config_t with Gemma 4 fields (ple_dim, swa_window, shared_kv_layers,
logit_softcap, rms_norm_eps, rope_theta_swa, head_dim_swa, per-layer arrays).
Reads explicit head_dim from GGUF instead of deriving dim/n_heads.
Existing Llama models: all new fields default to 0/NULL, zero behavior change."
```

---

## Task 2: Per-Layer Array Loading (SWA pattern, KV share map, FFN dims)

**Files:**
- Modify: `phase3/src/ai/llama_load.c` (extend config loading)
- Modify: `phase3/src/ai/gguf_parser.c` (add `gguf_find_kv()` to expose raw kv_t for array iteration)
- Modify: `phase3/src/ai/test_gemma4_config.c` (add tests)

- [ ] **Step 1: Write failing test for per-layer SWA pattern and KV share map**

```c
static void test_per_layer_arrays(void)
{
    TEST("per-layer SWA pattern + KV share map");
    uint8_t gguf_buf[16384];
    size_t gguf_len = 0;
    build_gemma4_gguf_with_arrays(gguf_buf, &gguf_len);

    gguf_ctx_t ctx;
    ASSERT(gguf_open_memory(&ctx, gguf_buf, gguf_len) == 0, "open");

    llama_config_t config;
    ASSERT(llama_load_config(&config, &ctx) == 0, "load config");

    /* Verify SWA pattern loaded */
    ASSERT(config.layer_is_swa != NULL, "layer_is_swa should be allocated");
    ASSERT(config.layer_is_swa[0] == true, "layer 0 should be SWA");
    ASSERT(config.layer_is_swa[4] == false, "layer 4 should be global");

    /* Verify KV share map */
    ASSERT(config.kv_share_map != NULL, "kv_share_map should be allocated");
    ASSERT(config.kv_share_map[0] == -1, "layer 0 should compute own KV");
    ASSERT(config.kv_share_map[14] == -1, "layer 14 should compute own KV");
    ASSERT(config.kv_share_map[15] >= 0, "layer 15 should share KV");

    gguf_close(&ctx);
    llama_free_config(&config);
    PASS();
}
```

- [ ] **Step 2: Run test — verify fails (arrays not populated)**

- [ ] **Step 3: Add `gguf_find_kv()` to `gguf_parser.c`**

```c
/* Expose raw KV entry for array iteration (needed for bool/int arrays) */
const gguf_kv_t *gguf_find_kv(const gguf_ctx_t *ctx, const char *key)
{
    for (uint64_t i = 0; i < ctx->n_kv; i++) {
        if (strcmp(ctx->kv[i].key, key) == 0)
            return &ctx->kv[i];
    }
    return NULL;
}
```

Add declaration to `gguf_parser.h`:
```c
const gguf_kv_t *gguf_find_kv(const gguf_ctx_t *ctx, const char *key);
```

- [ ] **Step 4: Implement per-layer array building in `llama_load_config()`**

After scalar metadata reading, add:

```c
    /* --- Build per-layer arrays --- */
    int nl = config->n_layers;

    /* KV share map: layers 0..(nl - shared_kv_layers - 1) compute own KV,
     * remaining layers share from the last layer with matching attention type */
    if (config->shared_kv_layers > 0 && nl > 0) {
        config->kv_share_map = (int *)malloc(nl * sizeof(int));
        if (!config->kv_share_map) return -1;
        int n_unique = nl - config->shared_kv_layers;
        for (int i = 0; i < nl; i++) {
            if (i < n_unique) {
                config->kv_share_map[i] = -1;  /* compute own KV */
            } else {
                /* Find last unique-KV layer with same attention type */
                /* For now: simple linear mapping (layer i shares from layer i % n_unique) */
                /* TODO: verify exact sharing pattern against llama.cpp reference */
                config->kv_share_map[i] = i % n_unique;
            }
        }
    }

    /* SWA pattern: read from GGUF bool array or infer */
    snprintf(key, sizeof(key), "%s.attention.sliding_window_pattern", arch);
    /* ... read bool array from GGUF KV if present ... */
    /* If absent but swa_window > 0: infer interleaved pattern */

    /* Per-layer FFN dim: read from GGUF int array if present, else uniform */
    /* ... similar pattern ... */
```

- [ ] **Step 5: Run tests — verify pass**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat: per-layer array loading — SWA pattern, KV share map, FFN dims"
```

---

## Task 3: Logit Softcapping

**Files:**
- Modify: `phase3/src/ai/llama_quant.c:453-455` (after output projection)
- Create (extend): `phase3/src/ai/test_gemma4_forward.c`

- [ ] **Step 1: Write failing test**

```c
/* test_gemma4_forward.c */
static void test_logit_softcap(void)
{
    TEST("logit softcapping: cap * tanh(x / cap)");
    float logits[4] = { 50.0f, -50.0f, 0.0f, 30.0f };
    float cap = 30.0f;
    /* Expected: 30*tanh(50/30)=29.25, 30*tanh(-50/30)=-29.25, 0, 30*tanh(1)=22.76 */

    logit_softcap(logits, 4, cap);

    ASSERT(fabsf(logits[0] - 29.25f) < 0.1f, "positive clamped ~29.25");
    ASSERT(fabsf(logits[1] + 29.25f) < 0.1f, "negative clamped ~-29.25");
    ASSERT(fabsf(logits[2]) < 0.01f, "zero stays zero");
    ASSERT(fabsf(logits[3] - 22.76f) < 0.1f, "30*tanh(1) ~22.76");
    PASS();
}
```

- [ ] **Step 2: Implement `logit_softcap()` in `llama_quant.c`**

Add static helper:
```c
static void logit_softcap(float *logits, int n, float cap)
{
    if (cap <= 0.0f) return;
    float inv_cap = 1.0f / cap;
    for (int i = 0; i < n; i++)
        logits[i] = cap * tanhf(logits[i] * inv_cap);
}
```

Insert call in `qmodel_forward()` after line 455 (`qmatmul_vec` output projection):
```c
    /* 4b. Logit softcapping (Gemma 4) */
    if (c->logit_softcap > 0.0f)
        logit_softcap(state->logits, c->vocab_size, c->logit_softcap);
```

- [ ] **Step 3: Run test — verify pass**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat: logit softcapping — cap * tanh(x / cap), gated by config.logit_softcap"
```

---

## Task 4: Per-Layer Embeddings (PLE)

**Files:**
- Modify: `phase3/src/ai/llama_quant.h:36-46` (extend qlayer_t + qmodel_t)
- Modify: `phase3/src/ai/llama_quant.c` (qmodel_load + qmodel_forward)
- Extend: `phase3/src/ai/test_gemma4_forward.c`

- [ ] **Step 1: Write failing test for PLE gate + residual add**

```c
static void test_ple_gate_add(void)
{
    TEST("PLE: gated residual addition to layer input");
    /* Mock: ple_contribution[dim=4] = {1,2,3,4}
     * gate[dim=4] = {0.5, -0.5, 1.0, 0.0} (before GELU)
     * x[dim=4] = {10, 20, 30, 40}
     * After GELU(gate) * ple + x: */
    float x[4] = {10, 20, 30, 40};
    float ple[4] = {1, 2, 3, 4};
    float gate[4] = {0.5f, -0.5f, 1.0f, 0.0f};
    /* GELU(0.5)~0.346, GELU(-0.5)~-0.154, GELU(1.0)~0.841, GELU(0.0)=0.0 */

    ple_gate_add(x, ple, gate, 4);

    ASSERT(fabsf(x[0] - (10.0f + 0.346f * 1.0f)) < 0.05f, "x[0]");
    ASSERT(fabsf(x[3] - 40.0f) < 0.01f, "x[3] unchanged (gate=0)");
    PASS();
}
```

- [ ] **Step 2: Extend `qlayer_t` and `qmodel_t`**

In `llama_quant.h`, add to `qlayer_t`:
```c
typedef struct {
    /* ... existing 9 fields ... */
    qtensor_t q_norm;          /* optional: per-head Q RMSNorm (Qwen3, Gemma4) */
    qtensor_t k_norm;          /* optional: per-head K RMSNorm (Qwen3, Gemma4) */
    qtensor_t ple_gate;        /* optional: PLE per-layer gate (Gemma4) */
} qlayer_t;
```

Add to `qmodel_t`:
```c
typedef struct {
    /* ... existing fields ... */
    qtensor_t ple_embed;       /* optional: PLE embedding table [vocab x ple_dim] */
    qtensor_t ple_proj;        /* optional: PLE projection [ple_dim x dim] */
    qtensor_t ple_norm;        /* optional: PLE projection norm [ple_dim] */
} qmodel_t;
```

- [ ] **Step 3: Implement PLE loading in `qmodel_load()`**

After existing global tensor loading:
```c
    /* PLE tensors (optional — Gemma 4) */
    if (c->ple_dim > 0) {
        resolve_qtensor(&qm->ple_embed, ctx, gguf_base, "token_embd_per_layer.weight");
        resolve_qtensor(&qm->ple_proj, ctx, gguf_base, "per_layer_model_proj.weight");
        resolve_qtensor(&qm->ple_norm, ctx, gguf_base, "per_layer_proj_norm.weight");
    }
```

In per-layer loop:
```c
        if (c->ple_dim > 0) {
            snprintf(name, sizeof(name), "blk.%d.cross_attn_attn_gate", i);
            /* Optional — skip if not found (non-Gemma4 models) */
            const gguf_tensor_info_t *gt = gguf_find_tensor(ctx, name);
            if (gt) {
                L->ple_gate.data = (const uint8_t *)gguf_base + ctx->data_offset + gt->offset;
                L->ple_gate.type = gt->type;
                L->ple_gate.n_elements = gt->n_elements;
                L->ple_gate.n_bytes = gt->n_bytes;
            }
        }
```

- [ ] **Step 4: Implement PLE in `qmodel_forward()`**

Before the layer loop, compute PLE embedding once:
```c
    /* PLE: embed token into per-layer space (once, reused by all layers) */
    float ple_buf[LLAMA_MAX_LAYERS * 256]; /* worst case: 128 layers * 256 ple_dim */
    float *ple_projected = NULL;
    if (c->ple_dim > 0 && qm->ple_embed.data) {
        float ple_raw[256]; /* ple_dim <= 256 */
        qembed_lookup(&qm->ple_embed, token, ple_raw, c->ple_dim);
        /* Project to model dim: ple_proj is [ple_dim x dim] */
        /* ... (qmatmul_vec or dequant + matmul) ... */
        ple_projected = ple_buf; /* points to projected PLE for this token */
    }
```

Inside layer loop, before the attention block:
```c
        /* PLE: gated residual add */
        if (ple_projected && layer->ple_gate.data) {
            /* gate = GELU(dequant(layer->ple_gate)) */
            float gate_vals[4096]; /* dim */
            dequant_row(layer->ple_gate.data, gate_vals, dim, (ggml_type_t)layer->ple_gate.type);
            for (int d = 0; d < dim; d++) {
                float g = gate_vals[d];
                /* GELU approximation: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715*x^3))) */
                float g3 = g * g * g;
                g = 0.5f * g * (1.0f + tanhf(0.7978845608f * (g + 0.044715f * g3)));
                state->x[d] += g * ple_projected[d];
            }
        }
```

- [ ] **Step 5: Run tests — verify pass**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat: PLE (per-layer embeddings) — token_embd_per_layer + gated residual add"
```

---

## Task 5: Sliding Window Attention + Per-Layer Dimensions

**Files:**
- Modify: `phase3/src/ai/llama_quant.c` (qmodel_forward attention loop)
- Modify: `phase3/src/ai/llama_load.c` (state allocation changes)
- Extend: `phase3/src/ai/test_gemma4_forward.c`

This is the largest task. The attention loop must branch on per-layer type.

- [ ] **Step 1: Write failing test for SWA masking**

```c
static void test_swa_mask(void)
{
    TEST("SWA: attention only sees last window positions");
    /* At position 100 with window=512, SWA layer should see positions 0..100 (all, since 100 < 512) */
    /* At position 600 with window=512, SWA layer should see positions 89..600 (last 512) */
    /* ... mock attention scores and verify masking ... */
    PASS();
}
```

- [ ] **Step 2: Modify attention loop in `qmodel_forward()` for per-layer branching**

The core change: the attention loop (lines 382-409) becomes per-layer-aware:

```c
        /* Per-layer attention parameters */
        int l_head_dim = (c->layer_is_swa && c->layer_is_swa[L]) ? c->head_dim_swa : c->head_dim;
        int l_kv_dim   = n_kv_heads * l_head_dim;
        int l_q_dim    = n_heads * l_head_dim;
        float l_rope_theta = (c->layer_is_swa && c->layer_is_swa[L]) ? c->rope_theta_swa : c->rope_theta;

        /* Q/K/V projections with per-layer dimensions */
        qmatmul_vec(&layer->wq, state->xb, state->q,  l_q_dim,  dim);
        qmatmul_vec(&layer->wk, state->xb, state->k,  l_kv_dim, dim);
        qmatmul_vec(&layer->wv, state->xb, state->v,  l_kv_dim, dim);

        /* RoPE with per-layer theta */
        int l_rope_dim = l_head_dim;  /* rotate full head_dim */
        for (int h = 0; h < n_heads; h++) {
            for (int i = 0; i < l_rope_dim / 2; i++) {
                float freq = 1.0f / powf(l_rope_theta, (float)(2 * i) / (float)l_head_dim);
                /* ... same rotation logic ... */
            }
        }

        /* Attention: SWA layers mask to [max(0, pos-window+1), pos] */
        int att_start = 0;
        if (c->swa_window > 0 && c->layer_is_swa && c->layer_is_swa[L]) {
            att_start = (pos >= c->swa_window) ? (pos - c->swa_window + 1) : 0;
        }

        for (int h = 0; h < n_heads; h++) {
            /* ... Score Q.K^T for positions att_start..pos ... */
            for (int t = att_start; t <= pos; t++) {
                /* ... existing scoring logic, but with l_head_dim and l_kv_dim ... */
            }
            tensor_softmax(att_h, att_h, pos + 1 - att_start);
            /* ... weighted V sum ... */
        }
```

- [ ] **Step 3: Adjust KV cache layout for variable per-layer sizes**

The current KV cache is `[n_layers * max_seq * kv_dim]` with uniform kv_dim. For Gemma 4, SWA layers have smaller kv_dim. Two approaches:
1. Allocate max kv_dim for all layers (wastes memory but simpler)
2. Allocate per-layer (saves memory but complex addressing)

**Choose option 1 for initial implementation** — allocate `max(head_dim, head_dim_swa) * n_kv_heads` per layer. This is simpler and the memory waste is bounded (512 vs 256 per head, 2x on SWA layers only).

- [ ] **Step 4: Run tests — verify pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat: sliding window attention + dual RoPE — per-layer head_dim, theta, window masking"
```

---

## Task 6: Dual RoPE + Q/K Norm

**Files:**
- Modify: `phase3/src/ai/llama_quant.c` (RoPE block + Q/K norm)
- Extend: `phase3/src/ai/test_gemma4_forward.c`

- [ ] **Step 1: Write failing test for Q/K norm**

```c
static void test_qk_norm(void)
{
    TEST("per-head Q RMSNorm before RoPE");
    /* 2 heads, head_dim=4, Q=[1,2,3,4, 5,6,7,8] */
    /* norm weights = [1,1,1,1] (identity), eps=1e-6 */
    /* After RMSNorm per head: each head's 4 values normalized independently */
    float q[8] = {1,2,3,4, 5,6,7,8};
    float norm_w[4] = {1,1,1,1};
    per_head_rms_norm(q, norm_w, 2, 4, 1e-6f);
    /* RMS of [1,2,3,4] = sqrt((1+4+9+16)/4) = sqrt(7.5) = 2.738 */
    /* normalized: [0.365, 0.730, 1.095, 1.461] */
    ASSERT(fabsf(q[0] - 0.365f) < 0.01f, "q[0] normalized");
    PASS();
}
```

- [ ] **Step 2: Implement per-head RMS norm + load Q/K norm tensors**

Add helper to `llama_quant.c`:
```c
static void per_head_rms_norm(float *x, const float *weight, int n_heads, int head_dim, float eps)
{
    for (int h = 0; h < n_heads; h++) {
        float *head = x + h * head_dim;
        float ss = 0.0f;
        for (int i = 0; i < head_dim; i++) ss += head[i] * head[i];
        ss = 1.0f / sqrtf(ss / (float)head_dim + eps);
        for (int i = 0; i < head_dim; i++) head[i] = head[i] * ss * weight[i];
    }
}
```

Load Q/K norm tensors in `qmodel_load()` per-layer loop:
```c
        /* Q/K norm (Qwen3, Gemma 4) — optional */
        snprintf(name, sizeof(name), "blk.%d.attn_q_norm.weight", i);
        {
            const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
            if (t) {
                L->q_norm.data = (const uint8_t *)gguf_base + ctx->data_offset + t->offset;
                L->q_norm.type = t->type;
                L->q_norm.n_elements = t->n_elements;
                L->q_norm.n_bytes = t->n_bytes;
            }
        }
        /* Same for attn_k_norm.weight */
```

Insert in `qmodel_forward()` between QKV projection and RoPE:
```c
        /* Q/K norm (before RoPE) */
        float norm_eps = c->rms_norm_eps > 0.0f ? c->rms_norm_eps : RMS_EPS;
        if (layer->q_norm.data) {
            float qn[512]; /* head_dim, max 512 for Gemma 4 */
            dequant_row(layer->q_norm.data, qn, l_head_dim, (ggml_type_t)layer->q_norm.type);
            per_head_rms_norm(state->q, qn, n_heads, l_head_dim, norm_eps);
        }
        if (layer->k_norm.data) {
            float kn[512];
            dequant_row(layer->k_norm.data, kn, l_head_dim, (ggml_type_t)layer->k_norm.type);
            per_head_rms_norm(state->k, kn, n_kv_heads, l_head_dim, norm_eps);
        }
```

- [ ] **Step 3: Run tests — verify pass**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat: per-head Q/K RMSNorm before RoPE — supports Qwen3 and Gemma 4"
```

---

## Task 7: Shared KV Cache

**Files:**
- Modify: `phase3/src/ai/llama_quant.c` (qmodel_forward KV handling)
- Modify: `phase3/src/ai/llama_load.c` (state allocation)
- Extend: `phase3/src/ai/test_gemma4_forward.c`

- [ ] **Step 1: Write failing test for KV sharing**

```c
static void test_shared_kv(void)
{
    TEST("shared KV: layer 15+ skips K/V projection, reuses layer 0's cache");
    /* Setup: kv_share_map[15] = 0 (share from layer 0)
     * Verify: after forward on layer 15, K/V cache for layer 15
     * is not written (stays zero), and attention reads from layer 0's cache slot. */
    /* ... mock config + state, run one layer's attention with sharing ... */
    PASS();
}
```

- [ ] **Step 2: Modify `qmodel_forward()` KV cache logic**

The key change: if `kv_share_map[L] >= 0`, skip K/V projection and use the source layer's cache:

```c
        int kv_layer = L;  /* which layer's KV cache to use */
        if (c->kv_share_map && c->kv_share_map[L] >= 0) {
            kv_layer = c->kv_share_map[L];
            /* Skip K/V projection — only compute Q */
            qmatmul_vec(&layer->wq, state->xb, state->q, l_q_dim, dim);
            /* K and V come from the source layer's cache (already stored) */
        } else {
            /* Normal path: compute Q, K, V */
            qmatmul_vec(&layer->wq, state->xb, state->q, l_q_dim, dim);
            qmatmul_vec(&layer->wk, state->xb, state->k, l_kv_dim, dim);
            qmatmul_vec(&layer->wv, state->xb, state->v, l_kv_dim, dim);

            /* Q/K norm + RoPE + store K/V in cache at kv_layer slot */
            /* ... (existing logic) ... */
        }

        /* Attention uses kv_layer's cache, not L's */
        int cache_offset = kv_layer * max_seq * max_kv_dim;
        float *key_layer = state->key_cache + cache_offset;
        float *val_layer = state->value_cache + cache_offset;
```

- [ ] **Step 3: Adjust state allocation for shared KV**

In `llama_alloc_state()`, the KV cache size should be based on the number of *unique* KV layers, not total layers:

```c
    int n_kv_layers = config->n_layers;
    if (config->shared_kv_layers > 0)
        n_kv_layers = config->n_layers - config->shared_kv_layers;

    int max_kv_dim = n_kv_heads * head_dim;
    if (config->head_dim_swa > 0) {
        int kv_dim_swa = n_kv_heads * config->head_dim_swa;
        if (kv_dim_swa > max_kv_dim) max_kv_dim = kv_dim_swa;
    }

    size_t kv_size = (size_t)n_kv_layers * (size_t)max_seq * (size_t)max_kv_dim;
```

**Note:** The `kv_layer` index used in the forward pass must map to the compressed cache layout. Add a `kv_cache_slot[n_layers]` mapping array: unique-KV layers get slots 0..(n_kv_layers-1), shared layers point to their source's slot.

- [ ] **Step 4: Run tests — verify pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat: shared KV cache — layers with kv_share_map>=0 skip K/V projection, reuse source"
```

---

## Task 8: Integration Test + Regression Guard

**Files:**
- Extend: `phase3/src/ai/test_gemma4_forward.c` (integration)
- Modify: `.github/workflows/ci.yml` (add forward test CI step)

- [ ] **Step 1: Write regression test — Llama 3.2 1B still works**

```c
static void test_llama_regression(void)
{
    TEST("Llama 3.2 1B: config loads with all new fields = 0/NULL");
    /* Build a Llama-arch GGUF, load config */
    /* Verify: ple_dim==0, swa_window==0, shared_kv_layers==0, logit_softcap==0 */
    /* Verify: kv_share_map==NULL, layer_is_swa==NULL, layer_ffn_dim==NULL */
    /* This proves the extended config doesn't break existing models */
    PASS();
}
```

- [ ] **Step 2: Write integration smoke test (requires model file)**

```c
static void test_gemma4_e2b_generation(void)
{
    TEST("Gemma 4 E2B: load + generate coherent text");
    const char *model_path = getenv("GEMMA4_E2B_PATH");
    if (!model_path) { printf("SKIP (no model)\n"); return; }

    /* Load GGUF */
    gguf_ctx_t ctx;
    if (gguf_open(&ctx, model_path) != 0) { FAIL("open"); return; }

    /* Load model */
    qmodel_t qm;
    if (qmodel_load(&qm, &ctx, /* mapped base */) != 0) { FAIL("load"); return; }

    /* Verify config loaded correctly */
    ASSERT(qm.config.dim == 1536, "dim=1536");
    ASSERT(qm.config.n_layers == 35, "n_layers=35");
    ASSERT(qm.config.ple_dim == 256, "ple_dim=256");
    ASSERT(qm.config.swa_window == 512, "swa_window=512");

    /* Alloc state + generate */
    llama_state_t state;
    llama_alloc_state(&state, &qm.config);

    int prompt[] = {2, 1616, 603};  /* "<bos> The capital" */
    int output[20];
    int n = qmodel_generate(&qm, &state, prompt, 3, output, 20, 1, 0.0f, 1, 42);
    ASSERT(n > 0, "generated >0 tokens");

    llama_free_state(&state);
    qmodel_free(&qm);
    gguf_close(&ctx);
    PASS();
}
```

- [ ] **Step 3: Add CI step for forward tests**

```yaml
    - name: "Phase 3: Gemma 4 Forward (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
          -I phase3/src/ai \
          phase3/src/ai/test_gemma4_forward.c \
          phase3/src/ai/llama_quant.c phase3/src/ai/llama_load.c \
          phase3/src/ai/gguf_parser.c phase3/src/ai/dequant.c \
          phase3/src/ai/tensor_ops.c phase3/src/ai/sampling.c \
          -lm -o /tmp/test_gemma4_forward
        /tmp/test_gemma4_forward
```

Note: integration test with actual model file will SKIP on CI (no 1.5GB model in CI).

- [ ] **Step 4: Run full test suite — verify zero regression**

```bash
# Run existing tests first
/tmp/test_llama_quant && /tmp/test_llama_forward && /tmp/test_inference
# Then new tests
/tmp/test_gemma4_config && /tmp/test_gemma4_forward
```

- [ ] **Step 5: Commit + push + verify CI**

```bash
git commit -m "test: Gemma 4 integration test + Llama regression guard"
git push origin feature/gemma4-arch
gh run list --limit 1  # verify CI green
```

---

## Task 9: Extend Fuzz Harness

**Files:**
- Modify: `phase3/src/drivers/fuzz_harness.c`

- [ ] **Step 1: Add fuzz target for SWA attention mask edge cases**

Fuzz the attention masking logic with random position values, window sizes, and layer types. Verify no out-of-bounds KV cache access.

- [ ] **Step 2: Add fuzz target for KV share map indexing**

Fuzz with random kv_share_map values, verify no array out-of-bounds on the KV cache.

- [ ] **Step 3: Run under ASAN**

```bash
gcc -Wall -Werror -O1 -std=c11 -fsanitize=address,undefined \
  -DJARVIS_TEST_MOCK -D_POSIX_C_SOURCE=200809L \
  -I phase3/src/drivers -I phase3/src/ipc -I phase3/src/ai \
  phase3/src/drivers/fuzz_harness.c \
  phase3/src/drivers/net_stack.c \
  phase3/src/ipc/shmem_ipc.c \
  phase3/src/ai/gguf_parser.c \
  -lm -o /tmp/fuzz_harness
/tmp/fuzz_harness
```

- [ ] **Step 4: Commit**

```bash
git commit -m "test: extend fuzz harness — SWA attention mask + shared KV indexing"
```

---

## Summary (UPDATED after 32MB byte-range verification)

| Task | Feature | Est LOC | New Tests | Key Risk |
|---|---|---|---|---|
| 1 | Config struct + arch-aware metadata | ~100 | 2 | None — pure plumbing |
| 2 | Per-layer arrays (SWA, KV map, FFN) | ~60 | 1 | Exact sharing pattern TBD — verify vs llama.cpp |
| 3 | Logit softcapping | ~15 | 1 | None — trivial |
| 3b | Sandwich norms + layer_output_scale | ~80 | 2 | NEW — 3 extra norms per layer + learned output scale. Discovered from GGUF byte dump (post_attention_norm, post_ffw_norm, post_norm, layer_output_scale tensors present in every layer). |
| 4 | PLE loading + forward | ~120 | 2 | Tensor names RESOLVED (see below) |
| 5 | Sliding window + per-layer dims | ~250 | 2 | Largest change — touches attention loop |
| 6 | Dual RoPE + Q/K norm | ~80 | 2 | Also unlocks Qwen3 |
| 7 | Shared KV cache | ~200 | 2 | KV cache addressing is the highest-risk code |
| 8 | Integration + regression | ~50 | 3 | Needs actual Gemma 4 E2B GGUF for smoke test |
| 9 | Fuzz harness extension | ~50 | 2 | ASAN must be clean |
| **Total** | | **~1005** | **19** | ±40% confidence band |

## Open Questions (resolve during implementation)

1. **PLE tensor names:** ~~The user byte-verified `blk.N.cross_attn_attn_gate`.~~ **RESOLVED.** 32MB byte-range download of `unsloth/gemma-4-E2B-it-GGUF` confirmed ALL tensor names (see updated reference above). Key corrections:
   - `per_layer_token_embd.weight` (not `token_embd_per_layer`)
   - `blk.N.inp_gate.weight` (not `cross_attn_attn_gate`)
   - `blk.N.proj.weight` — PLE per-layer projection (newly discovered)
   - `blk.N.post_attention_norm.weight` — sandwich norm (newly discovered)
   - `blk.N.post_ffw_norm.weight` — sandwich norm (newly discovered)
   - `blk.N.post_norm.weight` — additional norm (newly discovered)
   - `blk.N.layer_output_scale.weight` — layer output scaling (newly discovered)

2. **Exact KV sharing pattern:** The plan assumes `layers 0-14 own KV, 15-34 share`. But which layer does each shared layer reuse from? llama.cpp's logic says "last layer with matching attention type that has its own KV." Need to trace the exact mapping before coding Task 7.

3. **Per-layer FFN dim array:** Gemma 4 has variable FFN sizes (6144 for layers 0-14, 12288 for 15-34). The GGUF stores this as an `arr[i32, 35]`. Need to verify the `gguf_find_kv()` helper can iterate this array.

4. **`LLAMA_MAX_SEQ_LEN` increase:** Current cap is 512. Gemma 4 has `context_length = 131072` and `sliding_window = 512`. For SWA layers, 512 is fine. For global layers, we still cap at `LLAMA_MAX_SEQ_LEN`. This limits Gemma 4's effective context to 512 tokens on the JARVIS engine. Acceptable for initial implementation; increase later if needed.

5. **Sandwich norm + layer_output_scale semantics:** Byte-verified that each layer has `post_attention_norm`, `post_ffw_norm`, `post_norm`, and `layer_output_scale` tensors. The forward-pass order (based on llama.cpp `gemma4-iswa.cpp` source) is likely:
   ```
   attn_out = wo @ attention(q_norm(Q), k_norm(K), V)
   attn_out = post_attention_norm(attn_out)
   x = x + attn_out
   ffn_out = ffn(ffn_norm(x))
   ffn_out = post_ffw_norm(ffn_out)
   x = x + ffn_out
   x = x * layer_output_scale   (learned scalar per layer)
   ```
   The role of `post_norm` vs `post_attention_norm`/`post_ffw_norm` needs clarification from the llama.cpp reference before coding Task 3b. It may be applied to the PLE contribution or as a final per-layer norm.

6. **Decoupled head_dim:** Gemma 4 E2B has `n_heads=8, head_dim=512, dim=1536` → `n_heads*head_dim=4096 ≠ dim`. Same decoupled-head-dim issue as Qwen3 4B. The existing `dim/n_heads` derivation (llama_load.c:69) would give 192, not 512. **Must** use the explicit `gemma4.attention.key_length` key from GGUF. This is handled in Task 1 but is a critical correctness dependency for all later tasks.
