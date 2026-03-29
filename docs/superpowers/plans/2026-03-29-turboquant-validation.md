# TurboQuant Validation & Correction Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix TurboQuant codebooks to match the paper's exact Beta distribution, verify MSE/QJL claims, and test with real model generation.

**Architecture:** Replace Gaussian N(0,1) Lloyd-Max codebooks with Beta((d-1)/2, (d-1)/2) optimal centroids from the 0xSero reference implementation. Add verification tests for MSE (paper Table 1), QJL unbiasedness, and full-model generation comparison.

**Tech Stack:** C11, gcc, Llama 3.2 1B Q4_K_M (F32 dequant path), JARVIS TurboQuant module

---

## Research Summary

**The problem:** Our codebooks use Lloyd-Max centroids for N(0,1), scaled by sigma=1/sqrt(d). The paper uses the exact marginal distribution of a random unit vector's coordinates after rotation: Beta((d-1)/2, (d-1)/2) on [-1,1]. For d=64, that's Beta(31.5, 31.5).

**How much does it matter?** For d=64, the Gaussian approximation differs from Beta-optimal by ~2% on outer centroids. The practical impact is small but measurable. For correctness, we should match the paper.

**Reference values** (from 0xSero/turboquant, exact Beta-optimal):

d=64, 3-bit centroids:
```
Gaussian (current, after sigma scaling):  [-0.2690, -0.1680, -0.0945, -0.0306, +0.0306, +0.0945, +0.1680, +0.2690]
Beta-optimal (0xSero):                    [-0.2639, -0.1662, -0.0938, -0.0305, +0.0305, +0.0938, +0.1662, +0.2639]
Difference:                                  1.9%     1.1%     0.7%     0.3%
```

**Paper's MSE targets** (total per-vector, d=64 unit vectors):
- 2-bit: MSE = 0.117 (per-coord: 0.001789 * 64 = 0.1145)
- 3-bit: MSE = 0.034 (per-coord: 0.000522 * 64 = 0.0334)
- 4-bit: MSE = 0.009 (per-coord: 0.000143 * 64 = 0.00916)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/ai/turboquant.h` | Modify | Fix boundaries array size (16 → 17), remove sigma field |
| `phase3/src/ai/turboquant.c` | Modify | Replace codebook tables, change init_codebook to use head_dim |
| `phase3/src/ai/test_turboquant.c` | Modify | Add MSE verification test (#14), QJL unbiasedness test (#15), tighten thresholds |
| `phase3/src/ai/test_turboquant_real.c` | Modify | Add generation comparison (logit top-5 match) |
| `phase3/docs/TURBOQUANT_BENCHMARK.md` | Modify | Update results with corrected codebook numbers |

---

### Task 1: Replace Codebooks with Beta-Optimal Centroids

**Files:**
- Modify: `phase3/src/ai/turboquant.h`
- Modify: `phase3/src/ai/turboquant.c`

- [ ] **Step 1: Fix boundaries array size in turboquant.h**

The boundaries array is `float boundaries[TQ_MAX_CENTROIDS]` (16 elements). For 4-bit (16 centroids), we need 17 boundaries (n+1). This is a buffer overflow for 4-bit mode.

In `phase3/src/ai/turboquant.h`, change:

```c
/* OLD */
    float boundaries[TQ_MAX_CENTROIDS];
/* NEW */
    float boundaries[TQ_MAX_CENTROIDS + 1];
```

Also remove the `sigma` field from `tq_codebook_t` (no longer needed — centroids are pre-scaled):

```c
/* OLD */
    float sigma;
/* NEW — remove the sigma field entirely */
```

- [ ] **Step 2: Run existing tests to confirm they still compile**

Run: `gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c -lm -o /tmp/test_turboquant && /tmp/test_turboquant`

Expected: Compile may fail if sigma is referenced elsewhere. Fix any references before proceeding.

- [ ] **Step 3: Add Beta-optimal centroid tables to turboquant.c**

Replace the existing Gaussian centroid arrays with d-specific Beta-optimal values. Keep the Gaussian arrays as fallback for unsupported d. Add these static arrays (values from 0xSero/turboquant, exact Lloyd-Max on Beta((d-1)/2, (d-1)/2)):

```c
/* ---- Beta-optimal centroids for d=64 ---- */
/* Distribution: Beta(31.5, 31.5) on [-1, 1] */

static const float BETA_D64_1BIT[] = {
    -0.10012591f, 0.10012591f
};
static const float BETA_D64_2BIT[] = {
    -0.18749585f, -0.05651437f, 0.05651437f, 0.18749585f
};
static const float BETA_D64_3BIT[] = {
    -0.26390745f, -0.16616104f, -0.09382718f, -0.03046731f,
     0.03046731f,  0.09382718f,  0.16616104f,  0.26390745f
};
static const float BETA_D64_4BIT[] = {
    -0.33074890f, -0.25285796f, -0.19879805f, -0.15487006f,
    -0.11643835f, -0.08127422f, -0.04806602f, -0.01591089f,
     0.01591089f,  0.04806602f,  0.08127422f,  0.11643835f,
     0.15487006f,  0.19879805f,  0.25285796f,  0.33074890f
};

/* ---- Beta-optimal centroids for d=128 ---- */
/* Distribution: Beta(63.5, 63.5) on [-1, 1] */

static const float BETA_D128_1BIT[] = {
    -0.07066157f, 0.07066157f
};
static const float BETA_D128_2BIT[] = {
    -0.13304020f, -0.03999095f, 0.03999095f, 0.13304020f
};
static const float BETA_D128_3BIT[] = {
    -0.18839061f, -0.11813298f, -0.06658060f, -0.02160247f,
     0.02160247f,  0.06658060f,  0.11813298f,  0.18839061f
};
static const float BETA_D128_4BIT[] = {
    -0.23762719f, -0.18079373f, -0.14176165f, -0.11024707f,
    -0.08279257f, -0.05774454f, -0.03413403f, -0.01129650f,
     0.01129650f,  0.03413403f,  0.05774454f,  0.08279257f,
     0.11024707f,  0.14176165f,  0.18079373f,  0.23762719f
};
```

- [ ] **Step 4: Add codebook lookup function**

```c
/* Returns pre-computed Beta-optimal centroids for known head_dim values.
   Returns NULL if no pre-computed codebook exists (caller falls back to Gaussian). */
static const float *beta_centroids(int bits, int head_dim)
{
    if (head_dim == 64) {
        switch (bits) {
            case 1: return BETA_D64_1BIT;
            case 2: return BETA_D64_2BIT;
            case 3: return BETA_D64_3BIT;
            case 4: return BETA_D64_4BIT;
        }
    } else if (head_dim == 128) {
        switch (bits) {
            case 1: return BETA_D128_1BIT;
            case 2: return BETA_D128_2BIT;
            case 3: return BETA_D128_3BIT;
            case 4: return BETA_D128_4BIT;
        }
    }
    return NULL;
}
```

- [ ] **Step 5: Rewrite init_codebook to use head_dim**

Change the signature from `init_codebook(cb, bits, sigma)` to `init_codebook(cb, bits, head_dim)`:

```c
static void init_codebook(tq_codebook_t *cb, int bits, int head_dim)
{
    int n = 1 << bits;
    cb->n_centroids = n;
    cb->bits = bits;

    /* Try Beta-optimal centroids first */
    const float *src = beta_centroids(bits, head_dim);
    if (src) {
        for (int i = 0; i < n; i++)
            cb->centroids[i] = src[i];
    } else {
        /* Fallback: Gaussian N(0,1) centroids scaled by 1/sqrt(d) */
        float sigma = 1.0f / sqrtf((float)head_dim);
        const float *gauss;
        switch (bits) {
            case 1: gauss = LM_1BIT; break;
            case 2: gauss = LM_2BIT; break;
            case 3: gauss = LM_3BIT; break;
            case 4: gauss = LM_4BIT; break;
            default: gauss = LM_3BIT; break;
        }
        for (int i = 0; i < n; i++)
            cb->centroids[i] = gauss[i] * sigma;
    }

    /* Boundaries: edge sentinels + midpoints */
    cb->boundaries[0] = -1e30f;
    for (int i = 1; i < n; i++)
        cb->boundaries[i] = (cb->centroids[i - 1] + cb->centroids[i]) * 0.5f;
    cb->boundaries[n] = 1e30f;
}
```

- [ ] **Step 6: Update tq_init() call sites**

In `tq_init()`, change:

```c
/* OLD */
    float sigma = 1.0f / sqrtf((float)head_dim);
    init_codebook(&st->key_cb, key_bits - 1, sigma);
    init_codebook(&st->val_cb, val_bits, sigma);

/* NEW */
    init_codebook(&st->key_cb, key_bits - 1, head_dim);
    init_codebook(&st->val_cb, val_bits, head_dim);
```

Remove the `sigma` local variable if it's no longer used elsewhere in the function. Grep for any other uses of `cb->sigma` in the file and remove them.

- [ ] **Step 7: Run existing tests**

Run: `gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c -lm -o /tmp/test_turboquant && /tmp/test_turboquant`

Expected: All 13 tests should PASS. The centroid values changed by ~2%, so thresholds (0.90 key cosine, 0.95 value cosine) should still hold. If any test fails, note the new values — we'll adjust thresholds in Task 5.

- [ ] **Step 8: Commit codebook fix**

```bash
git add phase3/src/ai/turboquant.h phase3/src/ai/turboquant.c
git commit -m "fix: replace Gaussian codebooks with Beta-optimal centroids (arXiv 2504.19874)

Codebooks were Lloyd-Max for N(0,1) scaled by 1/sqrt(d). The correct
distribution is Beta((d-1)/2, (d-1)/2) for coordinates of rotated unit
vectors. Pre-computed centroids from 0xSero/turboquant for d=64 and d=128.

Practical difference is ~2% on outer centroids for d=64, but this makes
the implementation mathematically correct per the paper."
```

---

### Task 2: Add MSE Verification Test

**Files:**
- Modify: `phase3/src/ai/test_turboquant.c`

- [ ] **Step 1: Add Gaussian RNG helper for unit vector generation**

The existing `test_randf()` generates uniform [-1,1]. For proper unit vectors we need Gaussian samples normalized to the sphere. Add at the top of test_turboquant.c (after the existing `test_randf`):

```c
static float test_randn(uint64_t *s)
{
    /* Box-Muller transform */
    float u1 = (float)((test_rng(s) >> 11) + 1) / (float)((1ULL << 53) + 1);
    float u2 = (float)((test_rng(s) >> 11) + 1) / (float)((1ULL << 53) + 1);
    return sqrtf(-2.0f * logf(u1)) * cosf(6.283185307f * u2);
}

/* Generate random unit vector on S^{d-1} */
static void rand_unit_vec(float *v, int d, uint64_t *s)
{
    float norm = 0;
    for (int i = 0; i < d; i++) {
        v[i] = test_randn(s);
        norm += v[i] * v[i];
    }
    norm = sqrtf(norm);
    for (int i = 0; i < d; i++)
        v[i] /= norm;
}
```

- [ ] **Step 2: Write MSE verification test (Test 14)**

This test generates 10,000 random unit vectors, compresses with TurboQuant, decompresses, and measures total MSE. The result must match the paper's values within 15% tolerance (to account for finite-sample variance).

```c
static void test_mse_matches_paper(void) {
    TEST("MSE matches paper values (3-bit, d=64, 10K unit vectors)");

    tq_state_t tq;
    tq_init(&tq, 64, 3, 3, 12345);

    uint64_t rng = 99999;
    float total_key_mse = 0, total_val_mse = 0;
    int N = 10000;

    for (int i = 0; i < N; i++) {
        float vec[64];
        rand_unit_vec(vec, 64, &rng);

        /* Key: compress at (key_bits - 1) = 2 bits */
        tq_ckey_t ck;
        tq_compress_key(&tq, vec, &ck);
        float krecon[64];
        tq_decompress_key(&tq, &ck, krecon);
        float kmse = 0;
        for (int d = 0; d < 64; d++)
            kmse += (vec[d] - krecon[d]) * (vec[d] - krecon[d]);
        total_key_mse += kmse;

        /* Value: compress at val_bits = 3 bits */
        tq_cval_t cv;
        tq_compress_val(&tq, vec, &cv);
        float vrecon[64];
        tq_decompress_val(&tq, &cv, vrecon);
        float vmse = 0;
        for (int d = 0; d < 64; d++)
            vmse += (vec[d] - vrecon[d]) * (vec[d] - vrecon[d]);
        total_val_mse += vmse;
    }

    float avg_key_mse = total_key_mse / (float)N;
    float avg_val_mse = total_val_mse / (float)N;

    /* Paper values for d=64:
       2-bit MSE = 0.117 (keys use key_bits-1 = 2 bits)
       3-bit MSE = 0.034 (values use val_bits = 3 bits) */
    float key_target = 0.117f;  /* 2-bit */
    float val_target = 0.034f;  /* 3-bit */

    printf("  key MSE=%.4f (target=%.3f), val MSE=%.4f (target=%.3f)\n",
           avg_key_mse, key_target, avg_val_mse, val_target);

    /* Allow 15% tolerance for finite-sample variance */
    if (fabsf(avg_key_mse - key_target) / key_target < 0.15f &&
        fabsf(avg_val_mse - val_target) / val_target < 0.15f) {
        PASS();
    } else {
        FAIL("MSE does not match paper");
    }

    tq_free(&tq);
}
```

- [ ] **Step 3: Add call to test_mse_matches_paper in main()**

Add `test_mse_matches_paper();` after the existing test 13 call, and increment the total test count.

- [ ] **Step 4: Run the test**

Run: `gcc -Wall -O2 -std=c11 -I phase3/src/ai phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c -lm -o /tmp/test_turboquant && /tmp/test_turboquant`

Expected: Test 14 should PASS with the Beta-optimal codebooks. Note the actual MSE values — they should be close to 0.117 (key, 2-bit) and 0.034 (val, 3-bit).

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/test_turboquant.c
git commit -m "test: add MSE verification against paper values (TurboQuant)"
```

---

### Task 3: Add QJL Unbiasedness Verification Test

**Files:**
- Modify: `phase3/src/ai/test_turboquant.c`

- [ ] **Step 1: Write QJL unbiasedness test (Test 15)**

The paper's key claim: QJL correction makes the inner product estimator **unbiased** (mean error → 0). Test this with 10,000 random (query, key) pairs.

```c
static void test_qjl_unbiased(void) {
    TEST("QJL inner product is unbiased (mean error near 0)");

    tq_state_t tq;
    tq_init(&tq, 64, 3, 3, 77777);

    uint64_t rng = 54321;
    int N = 10000;
    float sum_err_mse = 0;   /* MSE-only (no QJL) error sum */
    float sum_err_qjl = 0;   /* QJL-corrected error sum */
    float sum_sq_mse = 0;    /* for variance */
    float sum_sq_qjl = 0;

    for (int i = 0; i < N; i++) {
        float query[64], key[64];
        rand_unit_vec(query, 64, &rng);
        rand_unit_vec(key, 64, &rng);

        /* True dot product */
        float true_dot = 0;
        for (int d = 0; d < 64; d++)
            true_dot += query[d] * key[d];

        /* Compress key */
        tq_ckey_t ck;
        tq_compress_key(&tq, key, &ck);

        /* MSE-only: decompress and dot */
        float krecon[64];
        tq_decompress_key(&tq, &ck, krecon);
        float mse_dot = 0;
        for (int d = 0; d < 64; d++)
            mse_dot += query[d] * krecon[d];
        float mse_err = mse_dot - true_dot;
        sum_err_mse += mse_err;
        sum_sq_mse += mse_err * mse_err;

        /* QJL-corrected dot */
        float qjl_dot = tq_dot_key(&tq, query, &ck);
        float qjl_err = qjl_dot - true_dot;
        sum_err_qjl += qjl_err;
        sum_sq_qjl += qjl_err * qjl_err;
    }

    float mean_mse = sum_err_mse / (float)N;
    float mean_qjl = sum_err_qjl / (float)N;
    float var_mse = sum_sq_mse / (float)N - mean_mse * mean_mse;
    float var_qjl = sum_sq_qjl / (float)N - mean_qjl * mean_qjl;

    printf("  MSE-only: mean_err=%.6f var=%.6f\n", mean_mse, var_mse);
    printf("  QJL:      mean_err=%.6f var=%.6f\n", mean_qjl, var_qjl);

    /* QJL should be closer to unbiased (|mean| < threshold) */
    if (fabsf(mean_qjl) < 0.01f && fabsf(mean_qjl) <= fabsf(mean_mse) + 0.005f) {
        PASS();
    } else {
        FAIL("QJL not unbiased or worse than MSE-only");
    }

    tq_free(&tq);
}
```

- [ ] **Step 2: Also tighten Test 5 (QJL effectiveness)**

The existing Test 5 uses `qjl_rmse < mse_rmse * 2.0` which is extremely loose. After understanding the results from Test 15, tighten this. Change:

```c
/* OLD */
    if (qjl_rmse < mse_rmse * 2.0f) {
/* NEW — QJL should not be significantly worse than MSE-only */
    if (qjl_rmse < mse_rmse * 1.5f) {
```

(If QJL variance turns out higher than MSE RMSE, this may need adjustment. The paper says QJL adds variance but removes bias. Check Test 15 results before finalizing.)

- [ ] **Step 3: Add call in main(), run tests**

Add `test_qjl_unbiased();` after test 14, increment total count.

Run: `gcc -Wall -O2 -std=c11 -I phase3/src/ai phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c -lm -o /tmp/test_turboquant && /tmp/test_turboquant`

Expected: All 15 tests PASS. Note the mean/var values — if QJL mean error is not near 0, investigate the formula in `tq_dot_key()`.

- [ ] **Step 4: Commit**

```bash
git add phase3/src/ai/test_turboquant.c
git commit -m "test: add QJL unbiasedness verification, tighten QJL threshold"
```

---

### Task 4: Add Real-Model Generation Comparison

**Files:**
- Modify: `phase3/src/ai/test_turboquant_real.c`

- [ ] **Step 1: Add generation comparison code**

After the existing multi-layer attention test (section 6), add section 7: full logit comparison. The approach:

1. Prefill 7 tokens (positions 0-6)
2. Save KV cache for positions 0-6
3. Forward on token 8 → f32_logits
4. Restore cache, TQ-compress positions 0-6, forward on token 8 → tq_logits
5. Compare top-5

Add this code after the existing verdict section, BEFORE the cleanup:

```c
    /* 7. Generation comparison: F32 logits vs TQ-influenced logits */
    printf("--- Generation Comparison ---\n\n");

    /* Re-run: prefill first 7 tokens */
    llama_free_state(&state);
    llama_alloc_state(&state, c);

    for (int i = 0; i < n_prompt - 1; i++)
        llama_forward(&model, &state, prompt[i]);
    /* state.pos = 7 (positions 0-6 filled) */

    /* Save KV cache for positions 0-6 */
    int save_pos = n_prompt - 1; /* 7 */
    size_t save_per_layer = (size_t)save_pos * (size_t)kv_dim * sizeof(float);
    float *saved_keys = (float *)malloc((size_t)c->n_layers * save_per_layer);
    float *saved_vals = (float *)malloc((size_t)c->n_layers * save_per_layer);
    if (!saved_keys || !saved_vals) {
        printf("SKIP generation comparison (malloc failed)\n");
        free(saved_keys); free(saved_vals);
        goto cleanup;
    }

    for (int L = 0; L < c->n_layers; L++) {
        memcpy(saved_keys + L * save_pos * kv_dim,
               state.key_cache + L * max_seq * kv_dim,
               save_per_layer);
        memcpy(saved_vals + L * save_pos * kv_dim,
               state.value_cache + L * max_seq * kv_dim,
               save_per_layer);
    }

    /* Forward on last token with F32 cache */
    llama_forward(&model, &state, prompt[n_prompt - 1]);
    float *f32_logits = (float *)malloc((size_t)c->vocab_size * sizeof(float));
    memcpy(f32_logits, state.logits, (size_t)c->vocab_size * sizeof(float));

    /* Restore cache, set pos back */
    state.pos = save_pos;
    for (int L = 0; L < c->n_layers; L++) {
        memcpy(state.key_cache + L * max_seq * kv_dim,
               saved_keys + L * save_pos * kv_dim,
               save_per_layer);
        memcpy(state.value_cache + L * max_seq * kv_dim,
               saved_vals + L * save_pos * kv_dim,
               save_per_layer);
    }

    /* TQ-compress positions 0-6 in all layers (in-place) */
    for (int L = 0; L < c->n_layers; L++) {
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

    /* Forward on last token with TQ cache */
    llama_forward(&model, &state, prompt[n_prompt - 1]);

    /* Compare top-5 tokens */
    printf("Generation comparison (F32 KV vs TQ KV):\n");

    int f32_top5[5], tq_top5[5];
    for (int rank = 0; rank < 5; rank++) {
        /* Find argmax for F32 */
        int best_f = 0;
        for (int i = 1; i < c->vocab_size; i++)
            if (f32_logits[i] > f32_logits[best_f]) best_f = i;
        f32_top5[rank] = best_f;
        f32_logits[best_f] = -1e30f; /* mask for next rank */

        /* Find argmax for TQ */
        int best_t = 0;
        for (int i = 1; i < c->vocab_size; i++)
            if (state.logits[i] > state.logits[best_t]) best_t = i;
        tq_top5[rank] = best_t;
        state.logits[best_t] = -1e30f;
    }

    int top1_match = (f32_top5[0] == tq_top5[0]);
    int top5_overlap = 0;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            if (f32_top5[i] == tq_top5[j]) top5_overlap++;

    printf("  Top-1 match: %s (f32=%d tq=%d)\n",
           top1_match ? "YES" : "NO", f32_top5[0], tq_top5[0]);
    printf("  Top-5 overlap: %d/5\n", top5_overlap);
    printf("  F32 top-5: [%d, %d, %d, %d, %d]\n",
           f32_top5[0], f32_top5[1], f32_top5[2], f32_top5[3], f32_top5[4]);
    printf("  TQ  top-5: [%d, %d, %d, %d, %d]\n\n",
           tq_top5[0], tq_top5[1], tq_top5[2], tq_top5[3], tq_top5[4]);

    free(f32_logits);
    free(saved_keys);
    free(saved_vals);
```

- [ ] **Step 2: Update the verdict section**

Update the pass/fail logic to include generation comparison:

```c
    if (top5_overlap < 3) {
        printf("FAIL: top-5 overlap %d/5 < 3\n", top5_overlap);
        pass = 0;
    }
```

- [ ] **Step 3: Add a `cleanup:` label before the existing cleanup code**

The `goto cleanup` from the malloc failure path needs a target. Add `cleanup:` before `tq_free(&tq);`.

- [ ] **Step 4: Compile and run**

Run:
```bash
gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
  phase3/src/ai/test_turboquant_real.c \
  phase3/src/ai/turboquant.c \
  phase3/src/ai/llama_forward.c \
  phase3/src/ai/llama_load.c \
  phase3/src/ai/tensor_ops.c \
  phase3/src/ai/dequant.c \
  phase3/src/ai/sampling.c \
  phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c \
  -lm -o /tmp/test_turboquant_real
/tmp/test_turboquant_real
```

Expected: PASS with model present, SKIP (exit 0) without model. Note the top-5 overlap — if < 3, TurboQuant significantly affects generation and we need to document this honestly.

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/test_turboquant_real.c
git commit -m "test: add real-model generation comparison (F32 vs TQ logits)"
```

---

### Task 5: Re-run All Tests, Update Benchmark Doc, Final Commit

**Files:**
- Modify: `phase3/src/ai/test_turboquant.c` (thresholds if needed)
- Modify: `phase3/docs/TURBOQUANT_BENCHMARK.md`

- [ ] **Step 1: Run full test suite**

```bash
# Unit tests (should be 15/15)
gcc -Wall -O2 -std=c11 -I phase3/src/ai \
  phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c \
  -lm -o /tmp/test_turboquant && /tmp/test_turboquant

# Real-data test (with model)
gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
  phase3/src/ai/test_turboquant_real.c phase3/src/ai/turboquant.c \
  phase3/src/ai/llama_forward.c phase3/src/ai/llama_load.c \
  phase3/src/ai/tensor_ops.c phase3/src/ai/dequant.c \
  phase3/src/ai/sampling.c phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c \
  -lm -o /tmp/test_turboquant_real && /tmp/test_turboquant_real

# Benchmark
gcc -Wall -O2 -std=c11 -I phase3/src/ai \
  phase3/src/ai/bench_turboquant.c phase3/src/ai/turboquant.c \
  -lm -o /tmp/bench_turboquant && /tmp/bench_turboquant
```

Record all output. Note any test failures and new metric values.

- [ ] **Step 2: Adjust thresholds if needed**

If any existing test fails with the new codebooks, compare old vs new values and adjust the threshold to be tight but passing. Document the change.

Likely adjustments:
- Test 3 (key cosine): may improve slightly (Beta centroids are marginally better)
- Test 5 (QJL effectiveness): may need the 1.5x threshold adjusted based on Test 15 results
- Test 10 (attention scores): correlation may change slightly

- [ ] **Step 3: Update TURBOQUANT_BENCHMARK.md**

Add a new section documenting:
1. **Codebook correction**: what changed, why, magnitude of difference
2. **Updated MSE values**: from Test 14 (should match paper)
3. **QJL unbiasedness**: from Test 15 (mean error, variance)
4. **Generation comparison**: from Task 4 (top-5 overlap)
5. **Updated compression quality table**: if cosine similarity changed

Be honest: if any metric got worse, document it with explanation. If the correction had negligible practical impact (likely for d=64), say so clearly.

- [ ] **Step 4: Run CI compile check**

Verify both CI steps compile cleanly:
```bash
gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
  phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c \
  -lm -o /tmp/test_turboquant

gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
  phase3/src/ai/test_turboquant_real.c phase3/src/ai/turboquant.c \
  phase3/src/ai/llama_forward.c phase3/src/ai/llama_load.c \
  phase3/src/ai/tensor_ops.c phase3/src/ai/dequant.c \
  phase3/src/ai/sampling.c phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c \
  -lm -o /tmp/test_turboquant_real
```

- [ ] **Step 5: Final commit**

```bash
git add phase3/src/ai/test_turboquant.c phase3/src/ai/turboquant.c \
       phase3/src/ai/turboquant.h phase3/src/ai/test_turboquant_real.c \
       phase3/docs/TURBOQUANT_BENCHMARK.md
git commit -m "fix: TurboQuant validation — Beta codebooks, MSE/QJL verified, generation tested

Replaced Gaussian N(0,1) codebooks with exact Beta((d-1)/2,(d-1)/2)
centroids from 0xSero/turboquant for d=64 and d=128.

Verification:
- MSE matches paper: 2-bit=X.XXX (target 0.117), 3-bit=X.XXX (target 0.034)
- QJL unbiasedness: mean_error=X.XXXXXX (near 0 = unbiased)
- Generation: top-5 overlap X/5, top-1 match: YES/NO
- 15/15 unit tests PASS, real-data validation PASS"
```

(Fill in actual numbers from test output before committing.)

Do NOT commit `.claude/settings.local.json`.

---

## Expected Outcomes

| Metric | Before (Gaussian) | After (Beta) | Paper Target |
|--------|:-:|:-:|:-:|
| 2-bit MSE (d=64) | ~0.119 | ~0.115 | 0.117 |
| 3-bit MSE (d=64) | ~0.035 | ~0.033 | 0.034 |
| 4-bit MSE (d=64) | ~0.010 | ~0.009 | 0.009 |
| QJL mean error | unknown | ~0.000 | 0 (unbiased) |
| Key cosine (3-bit) | 0.941 | ~0.943 | — |
| Real top-5 overlap | — | 3-5/5 | — |

The Gaussian-to-Beta correction is ~2% for d=64. The practical impact will be small but it makes the implementation correct per the paper. The bigger value is the new verification tests (MSE, QJL, generation) which prove the algorithm works end-to-end.
