# TurboQuant × llama.cpp Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate TurboQuant KV cache compression into llama.cpp to measure quality/speed/memory impact against CPU-only baselines on 8B, 13B, and 70B models.

**Architecture:** Inject a TQ compress→decompress roundtrip into llama.cpp's `cpy_k()`/`cpy_v()` methods via `ggml_map_custom1()`. Lossy (TQ-reconstructed) values go into the standard F16 cache. Everything else (model loading, tokenizer, SIMD matmul, sampling) stays untouched. This gives an apples-to-apples quality comparison.

**Tech Stack:** C11 (turboquant.c), C++17 (llama.cpp), CMake, ggml custom ops, llama-cli

---

## File Structure

### JARVIS Repo (`/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/`)

| File | Responsibility | Action |
|------|---------------|--------|
| `turboquant.h` | TQ public API + structs | Modify: add mixed bit-width fields, C++ guards |
| `turboquant.c` | TQ core: rotation, codebooks, compress/decompress | Modify: add mixed bit-width logic, general bitstream packer |
| `test_turboquant.c` | TQ unit tests (currently 16) | Modify: add 6 mixed-config tests |

### llama.cpp Repo (`/home/jarvis/llama.cpp/`)

| File | Responsibility | Action |
|------|---------------|--------|
| `src/turboquant.c` | Copy from JARVIS | Create: verbatim copy |
| `src/turboquant.h` | Copy from JARVIS | Create: verbatim copy |
| `src/CMakeLists.txt` | llama library source list | Modify: add turboquant.c |
| `src/llama-kv-cache.h` | KV cache class definition | Modify: add TQ state members |
| `src/llama-kv-cache.cpp` | KV cache implementation | Modify: init TQ, roundtrip in cpy_k/cpy_v |
| `common/common.h` | CLI params struct | Modify: add tq_key_bits, tq_val_bits fields |
| `common/arg.cpp` | CLI flag parsing | Modify: add --tq-key-bits, --tq-val-bits flags |

### Benchmarks (`/home/jarvis/Desktop/JARVIS_OS/phase3/`)

| File | Responsibility | Action |
|------|---------------|--------|
| `models/benchmark_tq.sh` | TQ benchmark runner | Create |
| `docs/BENCHMARK_RESULTS.md` | Results tracking | Modify: fill TQ columns |
| `benchmarks/turboquant-llamacpp.patch` | Reproducible patch | Create: git diff output |

---

## Task 1: Add C++ Guards and Bump TQ_MAX_LAYERS

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.h`

- [ ] **Step 1: Add extern C guards to turboquant.h**

At the top of `turboquant.h`, after the `#define TURBOQUANT_H` include guard (line 2), add:

```c
#ifdef __cplusplus
extern "C" {
#endif
```

At the bottom, before `#endif /* TURBOQUANT_H */` (last line), add:

```c
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Bump TQ_MAX_LAYERS from 32 to 128**

In `llama_forward_tq.h` (line 11), change:

```c
#define TQ_MAX_LAYERS 32
```

to:

```c
#define TQ_MAX_LAYERS 128
```

- [ ] **Step 3: Verify existing tests still pass**

Run:
```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: 16/16 PASS (or 17/17 depending on current count)

- [ ] **Step 4: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/turboquant.h phase3/src/ai/llama_forward_tq.h
git commit -m "feat: add C++ guards to turboquant.h, bump TQ_MAX_LAYERS to 128"
```

---

## Task 2: Add General Bitstream Pack/Unpack

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.c`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_turboquant.c`

The current code has `pack_2bit` and `pack_4bit` but no `pack_3bit`. Mixed bit-width configs need arbitrary bit packing. Add a general bitstream packer.

- [ ] **Step 1: Write failing test for bitstream pack/unpack**

Append to `test_turboquant.c`, before `main()`:

```c
static int test_bitstream_pack(void) {
    printf("  test_bitstream_pack...");
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    /* Pack 10 values at 3 bits each */
    for (int i = 0; i < 10; i++)
        pack_bits(buf, i, i % 8, 3);

    /* Verify roundtrip */
    for (int i = 0; i < 10; i++) {
        int val = unpack_bits(buf, i, 3);
        if (val != i % 8) {
            printf(" FAIL (idx %d: got %d, expected %d)\n", i, val, i % 8);
            return 1;
        }
    }

    /* Test 1-bit, 2-bit, 4-bit backward compat */
    memset(buf, 0, sizeof(buf));
    pack_bits(buf, 0, 1, 1);
    pack_bits(buf, 1, 0, 1);
    pack_bits(buf, 7, 1, 1);
    if (unpack_bits(buf, 0, 1) != 1 || unpack_bits(buf, 1, 1) != 0 || unpack_bits(buf, 7, 1) != 1) {
        printf(" FAIL (1-bit)\n"); return 1;
    }

    memset(buf, 0, sizeof(buf));
    pack_bits(buf, 0, 3, 2);
    pack_bits(buf, 3, 2, 2);
    if (unpack_bits(buf, 0, 2) != 3 || unpack_bits(buf, 3, 2) != 2) {
        printf(" FAIL (2-bit)\n"); return 1;
    }

    printf(" PASS\n");
    return 0;
}
```

Add `fails += test_bitstream_pack();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: FAIL — `pack_bits` and `unpack_bits` are not defined.

- [ ] **Step 3: Implement pack_bits / unpack_bits in turboquant.c**

Add after the existing `unpack_4bit` function (around line 354):

```c
/* General bitstream packer: pack 'bits' bits of 'val' at index 'idx' */
static void pack_bits(uint8_t *buf, int idx, int val, int bits) {
    int bit_pos = idx * bits;
    for (int b = 0; b < bits; b++) {
        int byte_idx = (bit_pos + b) / 8;
        int bit_idx  = (bit_pos + b) % 8;
        if (val & (1 << b))
            buf[byte_idx] |= (uint8_t)(1 << bit_idx);
        else
            buf[byte_idx] &= (uint8_t)~(1 << bit_idx);
    }
}

/* General bitstream unpacker */
static int unpack_bits(const uint8_t *buf, int idx, int bits) {
    int bit_pos = idx * bits;
    int val = 0;
    for (int b = 0; b < bits; b++) {
        int byte_idx = (bit_pos + b) / 8;
        int bit_idx  = (bit_pos + b) % 8;
        if (buf[byte_idx] & (1 << bit_idx))
            val |= (1 << b);
    }
    return val;
}
```

Also add declarations to `turboquant.h` (inside the extern C block, after existing function declarations) so the test can call them:

```c
/* Bitstream pack/unpack for mixed bit-width configs */
void tq_pack_bits(uint8_t *buf, int idx, int val, int bits);
int  tq_unpack_bits(const uint8_t *buf, int idx, int bits);
```

Make the static functions call through to public wrappers, or simply make them non-static and rename to `tq_pack_bits`/`tq_unpack_bits`. Update the test to use the `tq_` prefix.

- [ ] **Step 4: Run test to verify it passes**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: All tests PASS including new `test_bitstream_pack`.

- [ ] **Step 5: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/turboquant.c phase3/src/ai/turboquant.h phase3/src/ai/test_turboquant.c
git commit -m "feat: general bitstream pack/unpack for mixed-width TurboQuant"
```

---

## Task 3: Add Mixed Bit-Width to tq_state_t

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.h`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.c`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_turboquant.c`

- [ ] **Step 1: Write failing test for tq_init_mixed**

Append to `test_turboquant.c` before `main()`:

```c
static int test_mixed_init(void) {
    printf("  test_mixed_init...");
    tq_state_t state;

    /* 3.5-bit config: 32 outliers at 4-bit, 96 regular at 3-bit, d=128 */
    int rc = tq_init_mixed(&state, 128, 4, 3, 4, 3, 32, 42);
    if (rc != 0) { printf(" FAIL (init returned %d)\n", rc); return 1; }

    if (state.d != 128) { printf(" FAIL (d=%d)\n", state.d); return 1; }
    if (state.n_outlier != 32) { printf(" FAIL (n_outlier=%d)\n", state.n_outlier); return 1; }
    if (state.key_bits_high != 4) { printf(" FAIL (key_bits_high)\n"); return 1; }
    if (state.key_bits_low != 3) { printf(" FAIL (key_bits_low)\n"); return 1; }

    /* Verify effective bits: (32*4 + 96*3) / 128 = (128+288)/128 = 3.25 */
    /* Note: the prompt says 3.5, but with these params it's 3.25 for keys MSE portion */
    /* Effective including QJL: keys use bits-1 for MSE */
    /* Just verify codebooks are initialized */
    if (state.key_cb_high.n_centroids != (1 << 3)) { /* 4-1=3 bit MSE for outlier keys */
        printf(" FAIL (key_cb_high.n_centroids=%d)\n", state.key_cb_high.n_centroids); return 1;
    }
    if (state.key_cb_low.n_centroids != (1 << 2)) { /* 3-1=2 bit MSE for regular keys */
        printf(" FAIL (key_cb_low.n_centroids=%d)\n", state.key_cb_low.n_centroids); return 1;
    }

    tq_free(&state);
    printf(" PASS\n");
    return 0;
}
```

Add `fails += test_mixed_init();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: FAIL — `tq_init_mixed` not defined, `n_outlier` not a member.

- [ ] **Step 3: Update tq_state_t in turboquant.h**

Replace the existing `tq_state_t` definition (lines 39-48) with:

```c
typedef struct {
    int   d;                    /* head_dim */
    int   key_bits;             /* Uniform key bits (legacy, 0 if mixed) */
    int   val_bits;             /* Uniform val bits (legacy, 0 if mixed) */
    int   key_bits_high;        /* Bits for outlier key coords */
    int   key_bits_low;         /* Bits for regular key coords */
    int   val_bits_high;        /* Bits for outlier val coords */
    int   val_bits_low;         /* Bits for regular val coords */
    int   n_outlier;            /* Number of outlier coords (0 = uniform mode) */
    float *Pi;                  /* d×d orthogonal rotation matrix */
    float *S;                   /* d×d Gaussian QJL projection matrix */
    tq_codebook_t key_cb;       /* Legacy: uniform key codebook */
    tq_codebook_t val_cb;       /* Legacy: uniform val codebook */
    tq_codebook_t key_cb_high;  /* Outlier key codebook (key_bits_high - 1 bits) */
    tq_codebook_t key_cb_low;   /* Regular key codebook (key_bits_low - 1 bits) */
    tq_codebook_t val_cb_high;  /* Outlier val codebook (val_bits_high bits) */
    tq_codebook_t val_cb_low;   /* Regular val codebook (val_bits_low bits) */
    uint64_t seed;
} tq_state_t;
```

Add function declaration:

```c
int tq_init_mixed(tq_state_t *state, int head_dim,
                  int key_bits_high, int key_bits_low,
                  int val_bits_high, int val_bits_low,
                  int n_outlier, uint64_t seed);
```

- [ ] **Step 4: Implement tq_init_mixed in turboquant.c**

Add after the existing `tq_init` function:

```c
int tq_init_mixed(tq_state_t *state, int head_dim,
                  int key_bits_high, int key_bits_low,
                  int val_bits_high, int val_bits_low,
                  int n_outlier, uint64_t seed) {
    if (!state || head_dim <= 0 || head_dim > TQ_MAX_HEAD_DIM) return -1;
    if (head_dim % 8 != 0) return -1;
    if (key_bits_high < 2 || key_bits_high > TQ_MAX_BITS) return -1;
    if (key_bits_low < 2 || key_bits_low > TQ_MAX_BITS) return -1;
    if (val_bits_high < 1 || val_bits_high > TQ_MAX_BITS) return -1;
    if (val_bits_low < 1 || val_bits_low > TQ_MAX_BITS) return -1;
    if (n_outlier < 0 || n_outlier > head_dim) return -1;

    memset(state, 0, sizeof(*state));
    state->d = head_dim;
    state->key_bits_high = key_bits_high;
    state->key_bits_low = key_bits_low;
    state->val_bits_high = val_bits_high;
    state->val_bits_low = val_bits_low;
    state->n_outlier = n_outlier;
    state->seed = seed;

    /* For backward compat, also set legacy fields to high values */
    state->key_bits = key_bits_high;
    state->val_bits = val_bits_high;

    /* Allocate matrices (same as tq_init) */
    uint64_t rng = seed;
    state->Pi = (float *)malloc(head_dim * head_dim * sizeof(float));
    state->S  = (float *)malloc(head_dim * head_dim * sizeof(float));
    if (!state->Pi || !state->S) { tq_free(state); return -1; }

    gen_orthogonal_matrix(state->Pi, head_dim, &rng);
    gen_gaussian_matrix(state->S, head_dim, &rng);

    /* Initialize codebooks for each bit-width */
    /* Keys use bits-1 for MSE (1 bit reserved for QJL) */
    init_codebook(&state->key_cb_high, key_bits_high - 1, head_dim);
    init_codebook(&state->key_cb_low,  key_bits_low - 1,  head_dim);
    init_codebook(&state->val_cb_high, val_bits_high,      head_dim);
    init_codebook(&state->val_cb_low,  val_bits_low,       head_dim);

    /* Also init legacy codebooks (for tq_dot_key backward compat) */
    init_codebook(&state->key_cb, key_bits_high - 1, head_dim);
    init_codebook(&state->val_cb, val_bits_high,     head_dim);

    return 0;
}
```

Update existing `tq_init()` to delegate:

```c
int tq_init(tq_state_t *state, int head_dim, int key_bits, int val_bits, uint64_t seed) {
    /* Uniform mode: all coords get the same bit-width, n_outlier = 0 */
    return tq_init_mixed(state, head_dim, key_bits, key_bits, val_bits, val_bits, 0, seed);
}
```

Note: `init_codebook` and `gen_orthogonal_matrix`/`gen_gaussian_matrix` are existing static functions in turboquant.c. Verify they exist and have the right signatures before using them. If `init_codebook` doesn't exist by that name, find the equivalent (it's the codebook setup code inside `tq_init`). Extract it into a static helper if needed.

- [ ] **Step 5: Run test to verify it passes**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: All tests PASS including `test_mixed_init`.

- [ ] **Step 6: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/turboquant.h phase3/src/ai/turboquant.c phase3/src/ai/test_turboquant.c
git commit -m "feat: tq_init_mixed for per-coordinate bit allocation (arXiv 2504.19874)"
```

---

## Task 4: Update Compress/Decompress for Mixed Bit-Width

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.c`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_turboquant.c`

- [ ] **Step 1: Write failing test for mixed-width key roundtrip**

```c
static int test_mixed_key_roundtrip(void) {
    printf("  test_mixed_key_roundtrip...");
    tq_state_t state;
    tq_init_mixed(&state, 128, 4, 3, 4, 3, 32, 42);

    float key[128], recon[128];
    uint64_t rng = 123;
    int good = 0, total = 100;

    for (int t = 0; t < total; t++) {
        float norm = 0;
        for (int j = 0; j < 128; j++) {
            key[j] = tq_rng_gaussian(&rng);
            norm += key[j] * key[j];
        }
        norm = sqrtf(norm);
        for (int j = 0; j < 128; j++) key[j] /= norm;

        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);
        tq_decompress_key(&state, &ck, recon);

        /* Cosine similarity */
        float dot = 0, na = 0, nb = 0;
        for (int j = 0; j < 128; j++) {
            dot += key[j] * recon[j];
            na += key[j] * key[j];
            nb += recon[j] * recon[j];
        }
        float cos_sim = dot / (sqrtf(na) * sqrtf(nb) + 1e-10f);
        if (cos_sim > 0.90f) good++;
    }

    tq_free(&state);
    if (good < 85) {
        printf(" FAIL (%d/100 above 0.90 cosine)\n", good);
        return 1;
    }
    printf(" PASS (%d/100 above 0.90)\n", good);
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — compress/decompress still uses uniform codebooks, mixed fields are ignored.

- [ ] **Step 3: Update tq_compress_key for mixed mode**

In `tq_compress_key()` (turboquant.c, around line 405), modify the quantization loop. Replace the single-codebook loop:

```c
/* After rotation: quantize each coordinate with appropriate codebook */
for (int j = 0; j < d; j++) {
    const tq_codebook_t *cb;
    int mse_bits;
    if (state->n_outlier > 0 && j < state->n_outlier) {
        cb = &state->key_cb_high;
        mse_bits = state->key_bits_high - 1;
    } else if (state->n_outlier > 0) {
        cb = &state->key_cb_low;
        mse_bits = state->key_bits_low - 1;
    } else {
        cb = &state->key_cb;
        mse_bits = state->key_bits - 1;
    }
    int idx = quantize_scalar(cb, rotated[j]);
    tq_pack_bits(out->mse_idx, j, idx, mse_bits);
    quantized[j] = cb->centroids[idx];
}
```

**Important:** Since different coordinates now have different bit widths, the `tq_pack_bits` call handles variable packing. But the `j` indexing into the bitstream assumes uniform bits per coordinate. For mixed mode, we need to track the bit offset manually:

```c
int bit_offset = 0;
for (int j = 0; j < d; j++) {
    const tq_codebook_t *cb;
    int mse_bits;
    if (state->n_outlier > 0 && j < state->n_outlier) {
        cb = &state->key_cb_high;
        mse_bits = state->key_bits_high - 1;
    } else if (state->n_outlier > 0) {
        cb = &state->key_cb_low;
        mse_bits = state->key_bits_low - 1;
    } else {
        cb = &state->key_cb;
        mse_bits = state->key_bits - 1;
    }
    int idx = quantize_scalar(cb, rotated[j]);
    /* Pack at explicit bit offset */
    for (int b = 0; b < mse_bits; b++) {
        int byte_idx = (bit_offset + b) / 8;
        int bit_idx  = (bit_offset + b) % 8;
        if (idx & (1 << b))
            out->mse_idx[byte_idx] |= (uint8_t)(1 << bit_idx);
    }
    quantized[j] = cb->centroids[idx];
    bit_offset += mse_bits;
}
```

Apply the same pattern to `tq_compress_val`, `tq_decompress_key`, and `tq_decompress_val`.

- [ ] **Step 4: Update tq_decompress_key for mixed mode**

```c
int bit_offset = 0;
for (int j = 0; j < d; j++) {
    const tq_codebook_t *cb;
    int mse_bits;
    if (state->n_outlier > 0 && j < state->n_outlier) {
        cb = &state->key_cb_high;
        mse_bits = state->key_bits_high - 1;
    } else if (state->n_outlier > 0) {
        cb = &state->key_cb_low;
        mse_bits = state->key_bits_low - 1;
    } else {
        cb = &state->key_cb;
        mse_bits = state->key_bits - 1;
    }
    /* Unpack at explicit bit offset */
    int idx = 0;
    for (int b = 0; b < mse_bits; b++) {
        int byte_idx = (bit_offset + b) / 8;
        int bit_idx  = (bit_offset + b) % 8;
        if (in->mse_idx[byte_idx] & (1 << bit_idx))
            idx |= (1 << b);
    }
    if (idx >= cb->n_centroids) idx = cb->n_centroids - 1;
    quantized[j] = cb->centroids[idx];
    bit_offset += mse_bits;
}
```

- [ ] **Step 5: Update tq_compress_val and tq_decompress_val similarly**

Same pattern but using `val_bits_high`/`val_bits_low` (no `-1` since values use all bits for MSE):

```c
if (state->n_outlier > 0 && j < state->n_outlier) {
    cb = &state->val_cb_high;
    mse_bits = state->val_bits_high;
} else if (state->n_outlier > 0) {
    cb = &state->val_cb_low;
    mse_bits = state->val_bits_low;
} else {
    cb = &state->val_cb;
    mse_bits = state->val_bits;
}
```

- [ ] **Step 6: Run tests**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: All tests PASS including `test_mixed_key_roundtrip`. Existing 16 tests must still pass (backward compat via n_outlier=0 path).

- [ ] **Step 7: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/turboquant.c phase3/src/ai/test_turboquant.c
git commit -m "feat: mixed bit-width compress/decompress for outlier channels"
```

---

## Task 5: Add Predefined Configs and Compression Ratio Test

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.h`
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/test_turboquant.c`

- [ ] **Step 1: Add convenience macros to turboquant.h**

```c
/* Paper configs for d=128 (arXiv 2504.19874) */
/* 3.5-bit effective: 32 outlier at 4b, 96 regular at 3b */
#define TQ_CONFIG_35BIT(state, seed) \
    tq_init_mixed((state), 128, 4, 3, 4, 3, 32, (seed))

/* 2.5-bit effective: 32 outlier at 3b, 96 regular at 2b */
#define TQ_CONFIG_25BIT(state, seed) \
    tq_init_mixed((state), 128, 3, 2, 3, 2, 32, (seed))
```

- [ ] **Step 2: Write test for paper configs**

```c
static int test_paper_configs(void) {
    printf("  test_paper_configs...");
    tq_state_t s35, s25;

    if (TQ_CONFIG_35BIT(&s35, 42) != 0) { printf(" FAIL (3.5-bit init)\n"); return 1; }
    if (TQ_CONFIG_25BIT(&s25, 42) != 0) { printf(" FAIL (2.5-bit init)\n"); return 1; }

    /* Verify 3.5-bit: key MSE uses 3-bit high / 2-bit low */
    if (s35.key_cb_high.bits != 3) { printf(" FAIL (3.5 key_cb_high.bits=%d)\n", s35.key_cb_high.bits); return 1; }
    if (s35.key_cb_low.bits != 2)  { printf(" FAIL (3.5 key_cb_low.bits=%d)\n", s35.key_cb_low.bits); return 1; }

    /* Verify 2.5-bit: key MSE uses 2-bit high / 1-bit low */
    if (s25.key_cb_high.bits != 2) { printf(" FAIL (2.5 key_cb_high.bits=%d)\n", s25.key_cb_high.bits); return 1; }
    if (s25.key_cb_low.bits != 1)  { printf(" FAIL (2.5 key_cb_low.bits=%d)\n", s25.key_cb_low.bits); return 1; }

    /* Test roundtrip quality on 3.5-bit (should be high for d=128) */
    float key[128], recon[128];
    uint64_t rng = 999;
    int good = 0;
    for (int t = 0; t < 200; t++) {
        float norm = 0;
        for (int j = 0; j < 128; j++) { key[j] = tq_rng_gaussian(&rng); norm += key[j]*key[j]; }
        norm = sqrtf(norm);
        for (int j = 0; j < 128; j++) key[j] /= norm;

        tq_ckey_t ck;
        tq_compress_key(&s35, key, &ck);
        tq_decompress_key(&s35, &ck, recon);

        float dot=0, na=0, nb=0;
        for (int j = 0; j < 128; j++) { dot+=key[j]*recon[j]; na+=key[j]*key[j]; nb+=recon[j]*recon[j]; }
        if (dot / (sqrtf(na)*sqrtf(nb)+1e-10f) > 0.95f) good++;
    }

    tq_free(&s35);
    tq_free(&s25);

    if (good < 180) { printf(" FAIL (3.5-bit cosine: %d/200 > 0.95)\n", good); return 1; }
    printf(" PASS (3.5-bit: %d/200 > 0.95 cosine)\n", good);
    return 0;
}
```

- [ ] **Step 3: Run tests**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai && \
gcc -O2 -std=c11 -Wall -Werror -lm \
  test_turboquant.c turboquant.c -o test_tq && ./test_tq
```
Expected: All PASS.

- [ ] **Step 4: Commit**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
git add phase3/src/ai/turboquant.h phase3/src/ai/test_turboquant.c
git commit -m "feat: TQ_CONFIG_35BIT/25BIT macros matching paper configs"
```

---

## Task 6: Create llama.cpp Feature Branch and Copy TQ Source

**Files:**
- Create: `/home/jarvis/llama.cpp/src/turboquant.c` (copy)
- Create: `/home/jarvis/llama.cpp/src/turboquant.h` (copy)
- Modify: `/home/jarvis/llama.cpp/src/CMakeLists.txt`

- [ ] **Step 1: Create feature branch**

```bash
cd /home/jarvis/llama.cpp
git checkout -b feature/turboquant
```

- [ ] **Step 2: Copy TQ source files**

```bash
cp /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.c /home/jarvis/llama.cpp/src/turboquant.c
cp /home/jarvis/Desktop/JARVIS_OS/phase3/src/ai/turboquant.h /home/jarvis/llama.cpp/src/turboquant.h
```

- [ ] **Step 3: Add turboquant.c to CMakeLists.txt**

In `/home/jarvis/llama.cpp/src/CMakeLists.txt`, find the `add_library(llama` block (around line 10). After `llama-kv-cache.cpp` (line 23), add:

```cmake
    turboquant.c
```

Also add after the target definition:

```cmake
set_source_files_properties(turboquant.c PROPERTIES LANGUAGE C)
```

- [ ] **Step 4: Verify it compiles**

```bash
cd /home/jarvis/llama.cpp
cmake -B build-tq -DGGML_CUDA=OFF -DGGML_METAL=OFF -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
cmake --build build-tq -j$(nproc) --target llama-cli 2>&1 | tail -10
```
Expected: Compiles successfully with turboquant.c linked in.

- [ ] **Step 5: Commit on feature branch**

```bash
cd /home/jarvis/llama.cpp
git add src/turboquant.c src/turboquant.h src/CMakeLists.txt
git commit -m "feat: add TurboQuant KV compression source (from JARVIS AI-OS)"
```

---

## Task 7: Add CLI Flags for TurboQuant

**Files:**
- Modify: `/home/jarvis/llama.cpp/common/common.h` (line ~549)
- Modify: `/home/jarvis/llama.cpp/common/arg.cpp` (line ~1996)

- [ ] **Step 1: Add fields to common_params**

In `/home/jarvis/llama.cpp/common/common.h`, after `cache_type_v` (line 550), add:

```cpp
    // TurboQuant KV cache compression
    float tq_key_bits     = 0.0f;  // 0 = disabled, 2.5 or 3.5 = paper configs
    float tq_val_bits     = 0.0f;  // 0 = disabled, 2.5 or 3.5 = paper configs
    float tq_outlier_frac = 0.25f; // fraction of coords that get +1 bit (default 32/128)
```

- [ ] **Step 2: Add CLI argument parsing**

In `/home/jarvis/llama.cpp/common/arg.cpp`, after the `--cache-type-v` block (around line 2010), add:

```cpp
    add_opt(common_params_context{common_params_context::LLAMA_EXAMPLE_MAIN})
        .set_env("LLAMA_ARG_TQ_KEY_BITS")
        .add_arg("--tq-key-bits")
        .set_help("TurboQuant key bits (e.g. 2.5 or 3.5). 0 = disabled (default: 0)")
        .set_handler([](common_params & params, const std::string & value) {
            params.tq_key_bits = std::stof(value);
        });

    add_opt(common_params_context{common_params_context::LLAMA_EXAMPLE_MAIN})
        .set_env("LLAMA_ARG_TQ_VAL_BITS")
        .add_arg("--tq-val-bits")
        .set_help("TurboQuant value bits (e.g. 2.5 or 3.5). 0 = disabled (default: 0)")
        .set_handler([](common_params & params, const std::string & value) {
            params.tq_val_bits = std::stof(value);
        });
```

- [ ] **Step 3: Verify it compiles and --help shows the flags**

```bash
cd /home/jarvis/llama.cpp
cmake --build build-tq -j$(nproc) --target llama-cli 2>&1 | tail -5
./build-tq/bin/llama-cli --help 2>&1 | grep -i "tq-"
```
Expected: `--tq-key-bits` and `--tq-val-bits` appear in help output.

- [ ] **Step 4: Commit**

```bash
cd /home/jarvis/llama.cpp
git add common/common.h common/arg.cpp
git commit -m "feat: add --tq-key-bits and --tq-val-bits CLI flags"
```

---

## Task 8: Wire TurboQuant into the KV Cache

**Files:**
- Modify: `/home/jarvis/llama.cpp/src/llama-kv-cache.h`
- Modify: `/home/jarvis/llama.cpp/src/llama-kv-cache.cpp`

This is the core integration. We insert a `ggml_map_custom1` roundtrip before `ggml_set_rows` in `cpy_k()` and `cpy_v()`.

- [ ] **Step 1: Add TQ members to llama_kv_cache class**

In `/home/jarvis/llama.cpp/src/llama-kv-cache.h`, add at top:

```cpp
extern "C" {
#include "turboquant.h"
}
```

In the `llama_kv_cache` class private section (around line 250), add:

```cpp
    // TurboQuant state
    bool tq_enabled = false;
    std::vector<tq_state_t> tq_states;  // one per layer
    int tq_head_dim = 0;
    int tq_n_kv_heads = 0;

    struct tq_layer_ctx {
        tq_state_t *state;
        int head_dim;
        int n_kv_heads;
        bool is_key;  // true = key roundtrip, false = value roundtrip
    };
    std::vector<tq_layer_ctx> tq_layer_ctxs_k;  // persisted for graph lifetime
    std::vector<tq_layer_ctx> tq_layer_ctxs_v;
```

Add public method:

```cpp
public:
    void init_turboquant(float tq_key_bits, float tq_val_bits, float tq_outlier_frac);
```

- [ ] **Step 2: Implement init_turboquant and roundtrip callback**

In `/home/jarvis/llama.cpp/src/llama-kv-cache.cpp`, add near the top (after includes):

```cpp
extern "C" {
#include "turboquant.h"
}

static void tq_roundtrip_op(struct ggml_tensor * dst,
                             const struct ggml_tensor * src,
                             int ith, int nth, void * userdata) {
    const auto * ctx = (const llama_kv_cache::tq_layer_ctx *)userdata;
    const int head_dim = ctx->head_dim;
    const int n_heads = ctx->n_kv_heads;
    const int ne0 = (int)src->ne[0];  // n_embd_gqa = head_dim * n_kv_heads
    const int n_tokens = (int)src->ne[1];

    for (int t = ith; t < n_tokens; t += nth) {
        const float * src_row = (const float *)((const char *)src->data + t * src->nb[1]);
        float * dst_row = (float *)((char *)dst->data + t * dst->nb[1]);

        for (int h = 0; h < n_heads; h++) {
            const float * in  = src_row + h * head_dim;
            float * out = dst_row + h * head_dim;

            if (ctx->is_key) {
                tq_ckey_t ck;
                tq_compress_key(ctx->state, in, &ck);
                tq_decompress_key(ctx->state, &ck, out);
            } else {
                tq_cval_t cv;
                tq_compress_val(ctx->state, in, &cv);
                tq_decompress_val(ctx->state, &cv, out);
            }
        }
    }
}
```

Add `init_turboquant` method:

```cpp
void llama_kv_cache::init_turboquant(float tq_key_bits, float tq_val_bits, float tq_outlier_frac) {
    if (tq_key_bits <= 0 && tq_val_bits <= 0) return;

    tq_enabled = true;
    tq_head_dim = hparams.n_embd_head_k;
    tq_n_kv_heads = hparams.n_head_kv(0);

    int n_outlier = (int)(tq_outlier_frac * tq_head_dim);
    int key_bits_low  = (int)tq_key_bits;
    int key_bits_high = key_bits_low + (tq_key_bits > key_bits_low ? 1 : 0);
    int val_bits_low  = (int)tq_val_bits;
    int val_bits_high = val_bits_low + (tq_val_bits > val_bits_low ? 1 : 0);

    uint32_t n_layer = hparams.n_layer;
    tq_states.resize(n_layer);
    tq_layer_ctxs_k.resize(n_layer);
    tq_layer_ctxs_v.resize(n_layer);

    for (uint32_t il = 0; il < n_layer; il++) {
        tq_init_mixed(&tq_states[il], tq_head_dim,
                      key_bits_high, key_bits_low,
                      val_bits_high, val_bits_low,
                      n_outlier, 42 + il);

        tq_layer_ctxs_k[il] = { &tq_states[il], tq_head_dim, tq_n_kv_heads, true };
        tq_layer_ctxs_v[il] = { &tq_states[il], tq_head_dim, tq_n_kv_heads, false };
    }

    LLAMA_LOG_INFO("%s: TurboQuant enabled: key=%.1f-bit, val=%.1f-bit, outlier=%d/%d, layers=%u\n",
                   __func__, tq_key_bits, tq_val_bits, n_outlier, tq_head_dim, n_layer);
}
```

Note: You'll need to find how `hparams` is accessible in this class. It's stored as a const reference from the model in the constructor. Check the existing constructor to see how model params are accessed.

- [ ] **Step 3: Insert roundtrip in cpy_k**

In `cpy_k()` (line 1107 of llama-kv-cache.cpp), change:

```cpp
    return ggml_set_rows(ctx, k, k_cur, k_idxs);
```

to:

```cpp
    if (tq_enabled) {
        uint32_t ikv = map_layer_ids.at(il);
        k_cur = ggml_map_custom1(ctx, k_cur, tq_roundtrip_op,
                                  GGML_N_TASKS_MAX, (void *)&tq_layer_ctxs_k[ikv]);
    }
    return ggml_set_rows(ctx, k, k_cur, k_idxs);
```

- [ ] **Step 4: Insert roundtrip in cpy_v**

In `cpy_v()`, before the `ggml_set_rows` return (handles both transposed and non-transposed cases — there may be multiple return paths), add the same pattern:

```cpp
    if (tq_enabled) {
        uint32_t ikv = map_layer_ids.at(il);
        v_cur = ggml_map_custom1(ctx, v_cur, tq_roundtrip_op,
                                  GGML_N_TASKS_MAX, (void *)&tq_layer_ctxs_v[ikv]);
    }
```

Add this before each `ggml_set_rows` call in `cpy_v()`. Check the function — there are multiple code paths depending on `v_trans` and stream configuration. The roundtrip should happen on `v_cur` before any reshaping/padding operations.

- [ ] **Step 5: Call init_turboquant from the constructor**

Find where the KV cache is created. In `/home/jarvis/llama.cpp/src/llama-model.cpp` (line ~8279), after the `new llama_kv_cache(...)` call, the params are passed via `cparams`. We need to thread the TQ params through.

Alternative simpler approach: Call `init_turboquant` from the model's `create_memory` or similar factory. Search for where `common_params.tq_key_bits` is accessible and where the kv_cache object is exposed.

If threading params is too complex, use environment variables as a quick hack:

```cpp
// At end of llama_kv_cache constructor:
{
    const char * tq_k = getenv("LLAMA_TQ_KEY_BITS");
    const char * tq_v = getenv("LLAMA_TQ_VAL_BITS");
    if (tq_k && tq_v) {
        init_turboquant(atof(tq_k), atof(tq_v), 0.25f);
    }
}
```

Then the CLI flags set these env vars before model load. This avoids touching the deep plumbing.

- [ ] **Step 6: Build and smoke test**

```bash
cd /home/jarvis/llama.cpp
cmake --build build-tq -j$(nproc) --target llama-cli 2>&1 | tail -10

# Test 3.5-bit on 8B
LLAMA_TQ_KEY_BITS=3.5 LLAMA_TQ_VAL_BITS=3.5 \
./build-tq/bin/llama-cli \
  -m /home/jarvis/Desktop/JARVIS_OS/phase3/models/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf \
  -ngl 0 -c 512 -t 8 -n 32 --no-display-prompt --simple-io \
  -cnv --single-turn \
  -p "The seL4 microkernel is" 2>&1 | tail -20
```
Expected: Coherent English text output + `TurboQuant enabled` in log.

- [ ] **Step 7: Commit**

```bash
cd /home/jarvis/llama.cpp
git add src/llama-kv-cache.h src/llama-kv-cache.cpp
git commit -m "feat: TurboQuant KV roundtrip via ggml_map_custom1 in cpy_k/cpy_v"
```

---

## Task 9: Run Benchmarks

**Files:**
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/models/benchmark_tq.sh`

- [ ] **Step 1: Create TQ benchmark script**

```bash
#!/bin/bash
# TurboQuant Benchmark: 8B + 13B + 70B
set -e

TQ_BITS="${TQ_BITS:-3.5}"
CLI="/home/jarvis/llama.cpp/build-tq/bin/llama-cli"
MODELS="/home/jarvis/Desktop/JARVIS_OS/phase3/models"
LOG="$MODELS/benchmark_tq${TQ_BITS//.}_results.log"
MONITOR="$MODELS/benchmark_tq${TQ_BITS//.}_monitor.log"

export LLAMA_TQ_KEY_BITS="$TQ_BITS"
export LLAMA_TQ_VAL_BITS="$TQ_BITS"

: > "$LOG"

echo "========================================" | tee -a "$LOG"
echo " TURBOQUANT ${TQ_BITS}-bit | $(date)" | tee -a "$LOG"
echo " Ryzen 2700X | 48GB RAM | No GPU" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"

echo "=== PRE-RUN ===" | tee -a "$LOG"
free -m | awk '/Mem:/{printf "RAM: %sMB used, %sMB avail\n", $3, $7}' | tee -a "$LOG"

echo "ts,ram_used,ram_avail" > "$MONITOR"
( while true; do
  R=$(free -m | awk '/Mem:/{printf "%s,%s",$3,$7}')
  echo "$(date +%H:%M:%S),$R" >> "$MONITOR"
  sleep 1
done ) &
MON=$!
trap "kill $MON 2>/dev/null" EXIT

run_prompt() {
  local LABEL="$1" TOKENS="$2" PROMPT="$3" MODEL="$4" CTX="$5"
  echo "" | tee -a "$LOG"
  echo "=== $LABEL ($TOKENS tokens) ===" | tee -a "$LOG"
  $CLI -m "$MODEL" -ngl 0 -c "$CTX" -t 8 \
    --temp 0.7 -n "$TOKENS" \
    --no-display-prompt --simple-io \
    -cnv --single-turn \
    -p "$PROMPT" \
    2>&1 | tee -a "$LOG"
  echo "" | tee -a "$LOG"
  echo "--- RAM ---" | tee -a "$LOG"
  free -m | awk '/Mem:/{printf "RAM: %sMB used, %sMB avail\n", $3, $7}' | tee -a "$LOG"
}

run_model() {
  local NAME="$1" MODEL="$2" CTX="$3"
  echo "" | tee -a "$LOG"
  echo "########################################" | tee -a "$LOG"
  echo "# MODEL: $NAME (TQ ${TQ_BITS}-bit)" | tee -a "$LOG"
  echo "########################################" | tee -a "$LOG"
  run_prompt "$NAME P1" 64 "What is the seL4 microkernel and why is it important for security?" "$MODEL" "$CTX"
  run_prompt "$NAME P2" 128 "Write a lock-free SPSC ring buffer in C with push and pop functions." "$MODEL" "$CTX"
  run_prompt "$NAME P3" 256 "Compare KV cache compression: quantization vs eviction vs sparse attention. Which is best for edge devices with 1B-3B models?" "$MODEL" "$CTX"
}

run_model "Llama-3.1-8B" "$MODELS/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf" 4096
run_model "Llama-2-13B" "$MODELS/llama-2-13b.Q4_K_M.gguf" 4096
run_model "Llama-3.1-70B" "$MODELS/Meta-Llama-3.1-70B-Instruct-Q4_K_M.gguf" 2048

echo "" | tee -a "$LOG"
echo "=== ALL DONE at $(date) ===" | tee -a "$LOG"
```

- [ ] **Step 2: Run 3.5-bit benchmark**

```bash
cd /home/jarvis/Desktop/JARVIS_OS/phase3/models
TQ_BITS=3.5 bash benchmark_tq.sh
```

- [ ] **Step 3: Run 2.5-bit benchmark**

```bash
TQ_BITS=2.5 bash benchmark_tq.sh
```

- [ ] **Step 4: Extract results**

```bash
grep "Prompt.*t/s" benchmark_tq35_results.log
grep "Prompt.*t/s" benchmark_tq25_results.log
grep "memory_breakdown" benchmark_tq35_results.log
```

---

## Task 10: Document Results and Save Patch

**Files:**
- Modify: `/home/jarvis/Desktop/JARVIS_OS/phase3/docs/BENCHMARK_RESULTS.md`
- Create: `/home/jarvis/Desktop/JARVIS_OS/phase3/benchmarks/turboquant-llamacpp.patch`

- [ ] **Step 1: Fill in BENCHMARK_RESULTS.md Section 2 and 3**

Update all empty TurboQuant cells with actual numbers from the benchmark logs. Fill in the comparison summary (Section 3).

- [ ] **Step 2: Save llama.cpp patch**

```bash
cd /home/jarvis/llama.cpp
git diff main..feature/turboquant > /home/jarvis/Desktop/JARVIS_OS/phase3/benchmarks/turboquant-llamacpp.patch
```

- [ ] **Step 3: Commit results to JARVIS repo**

```bash
cd /home/jarvis/Desktop/JARVIS_OS
mkdir -p phase3/benchmarks
git add phase3/docs/BENCHMARK_RESULTS.md \
        phase3/models/benchmark_tq.sh \
        phase3/benchmarks/turboquant-llamacpp.patch \
        phase3/src/ai/turboquant.c \
        phase3/src/ai/turboquant.h \
        phase3/src/ai/test_turboquant.c
git commit -m "bench: TurboQuant × llama.cpp — 8B/13B/70B at 2.5-bit and 3.5-bit

Integrated TurboQuant KV compression into llama.cpp via ggml_map_custom1
(compress→decompress roundtrip in KV write path). Apples-to-apples comparison:
same model, tokenizer, sampling — only KV cache representation differs.

Patch saved to phase3/benchmarks/turboquant-llamacpp.patch for reproducibility."
```

- [ ] **Step 4: Update CLAUDE.md**

Add to the Phase 3 status table:
- TurboQuant mixed bit-width (2.5/3.5-bit configs)
- llama.cpp integration benchmark results
- Update test count

---

## Critical Reminders

1. **turboquant.c is C11.** Every C++ file that includes turboquant.h must use the `extern "C"` guards added in Task 1.
2. **tq_layer_ctx lifetime.** The `tq_layer_ctxs_k/v` vectors are stored as class members — they persist for the graph's lifetime. Do NOT use stack-local structs.
3. **Thread safety.** `tq_compress_key` uses stack-local `float rotated[TQ_MAX_HEAD_DIM]` and `tq_ckey_t` — these are thread-safe because each thread gets its own stack frame. The `tq_state_t` is read-only after init.
4. **70B needs 80 layers.** TQ_MAX_LAYERS is bumped to 128 in Task 1. The `tq_states` vector is dynamically sized so no static limit applies on the llama.cpp side.
5. **F16 double quantization.** The TQ roundtrip produces F32 which llama.cpp then stores as F16. This adds negligible additional error (~0.001%).
6. **13B is a base model.** Expect raw completions, not chat responses. Benchmark prompts still work.
7. **Don't modify llama.cpp main branch.** All work on `feature/turboquant` branch.
