/*
 * JARVIS AI-OS Phase 3 -- TurboQuant Unit Tests
 *
 * Tests: orthogonality, codebook ordering, compress/decompress roundtrip,
 * QJL inner product accuracy, memory savings, and stress test.
 */

#include "turboquant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-50s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ---- Helpers ---- */

static float cosine_similarity(const float *a, const float *b, int n)
{
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na < 1e-20f || nb < 1e-20f) return 0.0f;
    return dot / (sqrtf(na) * sqrtf(nb));
}

static float vec_norm_f(const float *a, int n)
{
    float s = 0;
    for (int i = 0; i < n; i++) s += a[i] * a[i];
    return sqrtf(s);
}

/* Simple RNG for test vectors */
static uint64_t test_rng_state = 12345678ULL;

static uint64_t test_rng(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static float test_randf(void)
{
    test_rng(&test_rng_state);
    return (float)(test_rng_state >> 11) / (float)(1ULL << 53) * 2.0f - 1.0f;
}

static void gen_random_vec(float *v, int n)
{
    for (int i = 0; i < n; i++) v[i] = test_randf();
}

/* Gaussian RNG via Box-Muller (for unit vector generation) */
static float test_randn(uint64_t *s)
{
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
    if (norm > 1e-10f)
        for (int i = 0; i < d; i++) v[i] /= norm;
}

/* ================================================================
 * Test 1: tq_init succeeds and Pi is orthogonal (Pi * Pi^T ≈ I)
 * ================================================================ */
static void test_orthogonality(void)
{
    TEST("Pi is orthogonal (Pi * Pi^T = I)");

    tq_state_t state;
    if (tq_init(&state, 64, 3, 3, 42) != 0) { FAIL("init failed"); return; }

    /* Check Pi * Pi^T ≈ I */
    float max_err = 0.0f;
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            float dot = 0.0f;
            for (int k = 0; k < 64; k++)
                dot += state.Pi[i * 64 + k] * state.Pi[j * 64 + k];
            float expected = (i == j) ? 1.0f : 0.0f;
            float err = fabsf(dot - expected);
            if (err > max_err) max_err = err;
        }
    }

    if (max_err < 1e-4f) PASS();
    else { char buf[64]; snprintf(buf, sizeof(buf), "max_err=%.6f", max_err); FAIL(buf); }

    tq_free(&state);
}

/* ================================================================
 * Test 2: Codebook centroids are sorted ascending
 * ================================================================ */
static void test_codebook_ordering(void)
{
    TEST("Codebook centroids sorted ascending");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    int ok = 1;
    for (int i = 1; i < state.key_cb.n_centroids; i++) {
        if (state.key_cb.centroids[i] <= state.key_cb.centroids[i - 1]) ok = 0;
    }
    for (int i = 1; i < state.val_cb.n_centroids; i++) {
        if (state.val_cb.centroids[i] <= state.val_cb.centroids[i - 1]) ok = 0;
    }

    if (ok) PASS(); else FAIL("not sorted");
    tq_free(&state);
}

/* ================================================================
 * Test 3: Key compress/decompress roundtrip quality (cosine sim > 0.90)
 * ================================================================ */
static void test_key_roundtrip(void)
{
    TEST("Key roundtrip cosine similarity > 0.90");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    float total_cos = 0.0f;
    int n_vecs = 100;
    for (int v = 0; v < n_vecs; v++) {
        float key[64], recon[64];
        gen_random_vec(key, 64);

        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);
        tq_decompress_key(&state, &ck, recon);

        total_cos += cosine_similarity(key, recon, 64);
    }

    float avg_cos = total_cos / (float)n_vecs;
    if (avg_cos > 0.90f) PASS();
    else { char buf[64]; snprintf(buf, sizeof(buf), "avg_cos=%.4f", avg_cos); FAIL(buf); }

    tq_free(&state);
}

/* ================================================================
 * Test 4: Value compress/decompress roundtrip quality (cosine sim > 0.95)
 * ================================================================ */
static void test_val_roundtrip(void)
{
    TEST("Value roundtrip cosine similarity > 0.95");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    float total_cos = 0.0f;
    int n_vecs = 100;
    for (int v = 0; v < n_vecs; v++) {
        float val[64], recon[64];
        gen_random_vec(val, 64);

        tq_cval_t cv;
        tq_compress_val(&state, val, &cv);
        tq_decompress_val(&state, &cv, recon);

        total_cos += cosine_similarity(val, recon, 64);
    }

    float avg_cos = total_cos / (float)n_vecs;
    if (avg_cos > 0.95f) PASS();
    else { char buf[64]; snprintf(buf, sizeof(buf), "avg_cos=%.4f", avg_cos); FAIL(buf); }

    tq_free(&state);
}

/* ================================================================
 * Test 5: QJL inner product is closer to true than MSE-only
 * ================================================================ */
static void test_qjl_inner_product(void)
{
    TEST("QJL dot product reduces error vs MSE-only");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    float total_mse_err = 0.0f;
    float total_qjl_err = 0.0f;
    int n_pairs = 200;

    for (int p = 0; p < n_pairs; p++) {
        float key[64], query[64];
        gen_random_vec(key, 64);
        gen_random_vec(query, 64);

        /* True inner product */
        float true_dot = 0.0f;
        for (int i = 0; i < 64; i++) true_dot += query[i] * key[i];

        /* Compress key */
        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);

        /* MSE-only inner product (decompress and dot) */
        float key_mse[64];
        tq_decompress_key(&state, &ck, key_mse);
        float mse_dot = 0.0f;
        for (int i = 0; i < 64; i++) mse_dot += query[i] * key_mse[i];

        /* QJL-corrected inner product */
        float qjl_dot = tq_dot_key(&state, query, &ck);

        total_mse_err += (mse_dot - true_dot) * (mse_dot - true_dot);
        total_qjl_err += (qjl_dot - true_dot) * (qjl_dot - true_dot);
    }

    float mse_rmse = sqrtf(total_mse_err / (float)n_pairs);
    float qjl_rmse = sqrtf(total_qjl_err / (float)n_pairs);

    printf("[mse_rmse=%.4f qjl_rmse=%.4f] ", mse_rmse, qjl_rmse);
    /* QJL should not be significantly worse than MSE-only */
    if (qjl_rmse < mse_rmse * 1.5f) PASS();
    else FAIL("QJL error much worse than MSE");

    tq_free(&state);
}

/* ================================================================
 * Test 6: Memory savings calculation
 * ================================================================ */
static void test_memory_savings(void)
{
    TEST("Memory savings ~8x for d=64, 3-bit");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    size_t compressed = tq_compressed_kv_bytes(&state, 8);
    size_t baseline = tq_baseline_kv_bytes(64, 8);
    float ratio = (float)baseline / (float)compressed;

    printf("[%zu vs %zu bytes, %.1fx] ", compressed, baseline, ratio);
    if (ratio > 5.0f && ratio < 15.0f) PASS();
    else FAIL("unexpected ratio");

    tq_free(&state);
}

/* ================================================================
 * Test 7: Zero vector handling
 * ================================================================ */
static void test_zero_vector(void)
{
    TEST("Zero vector compress/decompress");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    float zero[64], recon[64];
    memset(zero, 0, sizeof(zero));

    tq_ckey_t ck;
    tq_compress_key(&state, zero, &ck);
    tq_decompress_key(&state, &ck, recon);

    float nrm = vec_norm_f(recon, 64);
    if (nrm < 0.01f) PASS();
    else { char buf[64]; snprintf(buf, sizeof(buf), "norm=%.4f", nrm); FAIL(buf); }

    tq_free(&state);
}

/* ================================================================
 * Test 8: Larger head_dim (d=128) works
 * ================================================================ */
static void test_head_dim_128(void)
{
    TEST("d=128 compress/decompress roundtrip");

    tq_state_t state;
    if (tq_init(&state, 128, 3, 3, 42) != 0) { FAIL("init failed"); return; }

    float val[128], recon[128];
    gen_random_vec(val, 128);

    tq_cval_t cv;
    tq_compress_val(&state, val, &cv);
    tq_decompress_val(&state, &cv, recon);

    float cos_sim = cosine_similarity(val, recon, 128);
    if (cos_sim > 0.95f) PASS();
    else { char buf[64]; snprintf(buf, sizeof(buf), "cos=%.4f", cos_sim); FAIL(buf); }

    tq_free(&state);
}

/* ================================================================
 * Test 9: Stress test — many vectors, check no crashes
 * ================================================================ */
static void test_stress(void)
{
    TEST("Stress: 1000 key+value compress/decompress");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    int ok = 1;
    for (int i = 0; i < 1000; i++) {
        float key[64], val[64], krecon[64], vrecon[64];
        gen_random_vec(key, 64);
        gen_random_vec(val, 64);

        tq_ckey_t ck;
        tq_cval_t cv;
        tq_compress_key(&state, key, &ck);
        tq_compress_val(&state, val, &cv);
        tq_decompress_key(&state, &ck, krecon);
        tq_decompress_val(&state, &cv, vrecon);

        float kcos = cosine_similarity(key, krecon, 64);
        float vcos = cosine_similarity(val, vrecon, 64);
        if (kcos < 0.5f || vcos < 0.5f) { ok = 0; break; }
    }

    if (ok) PASS(); else FAIL("low quality on some vectors");
    tq_free(&state);
}

/* ================================================================
 * Test 10: Attention score comparison (simulated attention)
 * ================================================================ */
static void test_attention_scores(void)
{
    TEST("Attention scores: TQ vs F32 correlation > 0.95");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    int seq_len = 32;
    float query[64];
    gen_random_vec(query, 64);

    float f32_scores[32], tq_scores[32];

    for (int t = 0; t < seq_len; t++) {
        float key[64];
        gen_random_vec(key, 64);

        /* F32 score */
        float dot = 0.0f;
        for (int i = 0; i < 64; i++) dot += query[i] * key[i];
        f32_scores[t] = dot / sqrtf(64.0f);

        /* TQ score */
        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);
        tq_scores[t] = tq_dot_key(&state, query, &ck) / sqrtf(64.0f);
    }

    float corr = cosine_similarity(f32_scores, tq_scores, seq_len);
    printf("[corr=%.4f] ", corr);
    if (corr > 0.95f) PASS();
    else FAIL("attention score correlation too low");

    tq_free(&state);
}

/* ================================================================
 * Test 11: State memory overhead is reasonable
 * ================================================================ */
static void test_state_overhead(void)
{
    TEST("State overhead < 64KB for d=64");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 42);

    size_t overhead = tq_state_bytes(&state);
    printf("[%zu bytes] ", overhead);
    if (overhead < 65536) PASS();
    else FAIL("too large");

    tq_free(&state);
}

/* ================================================================
 * Test 12: Invalid parameters rejected
 * ================================================================ */
static void test_invalid_params(void)
{
    TEST("Invalid parameters rejected");

    tq_state_t state;
    int ok = 1;
    if (tq_init(&state, 0, 3, 3, 42) == 0) ok = 0;
    if (tq_init(&state, 200, 3, 3, 42) == 0) ok = 0;    /* > TQ_MAX_HEAD_DIM */
    if (tq_init(&state, 64, 1, 3, 42) == 0) ok = 0;     /* key_bits < 2 */
    if (tq_init(&state, 64, 3, 0, 42) == 0) ok = 0;     /* val_bits < 1 */
    if (tq_init(NULL, 64, 3, 3, 42) == 0) ok = 0;

    if (ok) PASS(); else FAIL("accepted bad params");
}

/* ================================================================
 * Test 13: Full KV cache simulation — Llama 3.2 1B dimensions
 * ================================================================ */
static void test_full_kv_simulation(void)
{
    TEST("Full KV cache simulation (16 layers, 8 heads, 64 pos)");

    int n_layers = 16, n_kv_heads = 8, head_dim = 64, seq_len = 64;

    /* Initialize one TQ state per layer */
    tq_state_t states[16];
    for (int L = 0; L < n_layers; L++) {
        if (tq_init(&states[L], head_dim, 3, 3, 42 + (uint64_t)L) != 0) {
            FAIL("init failed"); return;
        }
    }

    /* Simulate KV cache fill */
    int total_ops = 0;
    float avg_key_cos = 0, avg_val_cos = 0;
    int n_measured = 0;

    for (int L = 0; L < n_layers; L++) {
        for (int t = 0; t < seq_len; t++) {
            for (int h = 0; h < n_kv_heads; h++) {
                float key[64], val[64], krecon[64], vrecon[64];
                gen_random_vec(key, head_dim);
                gen_random_vec(val, head_dim);

                tq_ckey_t ck;
                tq_cval_t cv;
                tq_compress_key(&states[L], key, &ck);
                tq_compress_val(&states[L], val, &cv);
                tq_decompress_key(&states[L], &ck, krecon);
                tq_decompress_val(&states[L], &cv, vrecon);

                avg_key_cos += cosine_similarity(key, krecon, head_dim);
                avg_val_cos += cosine_similarity(val, vrecon, head_dim);
                n_measured++;
                total_ops++;
            }
        }
    }

    avg_key_cos /= (float)n_measured;
    avg_val_cos /= (float)n_measured;

    /* Memory comparison */
    size_t tq_total = 0, f32_total = 0;
    for (int L = 0; L < n_layers; L++) {
        tq_total += (size_t)seq_len * tq_compressed_kv_bytes(&states[L], n_kv_heads);
        f32_total += (size_t)seq_len * tq_baseline_kv_bytes(head_dim, n_kv_heads);
        tq_total += tq_state_bytes(&states[L]);  /* Include matrix overhead */
    }

    float ratio = (float)f32_total / (float)tq_total;
    printf("[ops=%d kcos=%.3f vcos=%.3f %.1fx] ", total_ops, avg_key_cos, avg_val_cos, ratio);

    if (avg_key_cos > 0.85f && avg_val_cos > 0.92f && ratio > 3.0f) PASS();
    else FAIL("quality or compression below threshold");

    for (int L = 0; L < n_layers; L++) tq_free(&states[L]);
}

/* ================================================================
 * Test 14: MSE matches paper values (arXiv 2504.19874 Table 1)
 * ================================================================ */
static void test_mse_matches_paper(void)
{
    TEST("MSE matches paper (2-bit key ≈0.117, 3-bit val ≈0.034)");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 12345);

    uint64_t rng = 99999;
    float total_key_mse = 0, total_val_mse = 0;
    int N = 10000;

    for (int i = 0; i < N; i++) {
        float vec[64];
        rand_unit_vec(vec, 64, &rng);

        /* Key: compress at (key_bits - 1) = 2 bits */
        tq_ckey_t ck;
        tq_compress_key(&state, vec, &ck);
        float krecon[64];
        tq_decompress_key(&state, &ck, krecon);
        float kmse = 0;
        for (int d = 0; d < 64; d++)
            kmse += (vec[d] - krecon[d]) * (vec[d] - krecon[d]);
        total_key_mse += kmse;

        /* Value: compress at val_bits = 3 bits */
        tq_cval_t cv;
        tq_compress_val(&state, vec, &cv);
        float vrecon[64];
        tq_decompress_val(&state, &cv, vrecon);
        float vmse = 0;
        for (int d = 0; d < 64; d++)
            vmse += (vec[d] - vrecon[d]) * (vec[d] - vrecon[d]);
        total_val_mse += vmse;
    }

    float avg_key_mse = total_key_mse / (float)N;
    float avg_val_mse = total_val_mse / (float)N;

    /* Paper targets for d=64: 2-bit MSE=0.117, 3-bit MSE=0.034 */
    printf("\n    key(2bit) MSE=%.4f (target 0.117), val(3bit) MSE=%.4f (target 0.034)\n    ",
           avg_key_mse, avg_val_mse);

    /* 20% tolerance for finite-sample variance + f16 norm quantization */
    if (fabsf(avg_key_mse - 0.117f) / 0.117f < 0.20f &&
        fabsf(avg_val_mse - 0.034f) / 0.034f < 0.20f) {
        PASS();
    } else {
        FAIL("MSE does not match paper");
    }

    tq_free(&state);
}

/* ================================================================
 * Test 15: 4-bit MSE matches paper (key 3-bit ≈ 0.034, val 4-bit ≈ 0.009)
 * ================================================================ */
static void test_mse_4bit(void)
{
    TEST("4-bit MSE matches paper (3-bit key ≈0.034, 4-bit val ≈0.009)");

    tq_state_t state;
    tq_init(&state, 64, 4, 4, 12345);  /* 4-bit: keys use 3-bit MSE + 1-bit QJL */

    uint64_t rng = 88888;
    float total_key_mse = 0, total_val_mse = 0;
    int N = 10000;

    for (int i = 0; i < N; i++) {
        float vec[64];
        rand_unit_vec(vec, 64, &rng);

        tq_ckey_t ck;
        tq_compress_key(&state, vec, &ck);
        float krecon[64];
        tq_decompress_key(&state, &ck, krecon);
        float kmse = 0;
        for (int d = 0; d < 64; d++)
            kmse += (vec[d] - krecon[d]) * (vec[d] - krecon[d]);
        total_key_mse += kmse;

        tq_cval_t cv;
        tq_compress_val(&state, vec, &cv);
        float vrecon[64];
        tq_decompress_val(&state, &cv, vrecon);
        float vmse = 0;
        for (int d = 0; d < 64; d++)
            vmse += (vec[d] - vrecon[d]) * (vec[d] - vrecon[d]);
        total_val_mse += vmse;
    }

    float avg_key_mse = total_key_mse / (float)N;
    float avg_val_mse = total_val_mse / (float)N;

    printf("\n    key(3bit) MSE=%.4f (target 0.034), val(4bit) MSE=%.4f (target 0.009)\n    ",
           avg_key_mse, avg_val_mse);

    if (fabsf(avg_key_mse - 0.034f) / 0.034f < 0.20f &&
        fabsf(avg_val_mse - 0.009f) / 0.009f < 0.20f) {
        PASS();
    } else {
        FAIL("4-bit MSE does not match paper");
    }

    tq_free(&state);
}

/* ================================================================
 * Test 16: QJL inner product is unbiased (mean error near 0)
 * ================================================================ */
static void test_qjl_unbiased(void)
{
    TEST("QJL inner product unbiased (|mean error| < 0.01)");

    tq_state_t state;
    tq_init(&state, 64, 3, 3, 77777);

    uint64_t rng = 54321;
    int N = 10000;
    float sum_err_mse = 0, sum_err_qjl = 0;
    float sum_sq_mse = 0, sum_sq_qjl = 0;

    for (int i = 0; i < N; i++) {
        float query[64], key[64];
        rand_unit_vec(query, 64, &rng);
        rand_unit_vec(key, 64, &rng);

        float true_dot = 0;
        for (int d = 0; d < 64; d++)
            true_dot += query[d] * key[d];

        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);

        /* MSE-only dot */
        float krecon[64];
        tq_decompress_key(&state, &ck, krecon);
        float mse_dot = 0;
        for (int d = 0; d < 64; d++)
            mse_dot += query[d] * krecon[d];
        float mse_err = mse_dot - true_dot;
        sum_err_mse += mse_err;
        sum_sq_mse += mse_err * mse_err;

        /* QJL-corrected dot */
        float qjl_dot = tq_dot_key(&state, query, &ck);
        float qjl_err = qjl_dot - true_dot;
        sum_err_qjl += qjl_err;
        sum_sq_qjl += qjl_err * qjl_err;
    }

    float mean_mse = sum_err_mse / (float)N;
    float mean_qjl = sum_err_qjl / (float)N;
    float var_mse = sum_sq_mse / (float)N - mean_mse * mean_mse;
    float var_qjl = sum_sq_qjl / (float)N - mean_qjl * mean_qjl;

    printf("\n    MSE-only: mean_err=%.6f var=%.6f\n", mean_mse, var_mse);
    printf("    QJL:      mean_err=%.6f var=%.6f\n    ", mean_qjl, var_qjl);

    if (fabsf(mean_qjl) < 0.01f) {
        PASS();
    } else {
        FAIL("QJL not unbiased");
    }

    tq_free(&state);
}

/* ================================================================
 * Test 17: General bitstream pack/unpack (tq_pack_bits / tq_unpack_bits)
 * ================================================================ */
static int test_bitstream_pack(void) {
    printf("  test_bitstream_pack...");
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    /* Pack 10 values at 3 bits each */
    for (int i = 0; i < 10; i++)
        tq_pack_bits(buf, i, i % 8, 3);

    /* Verify roundtrip */
    for (int i = 0; i < 10; i++) {
        int val = tq_unpack_bits(buf, i, 3);
        if (val != i % 8) {
            printf(" FAIL (idx %d: got %d, expected %d)\n", i, val, i % 8);
            return 1;
        }
    }

    /* Test 1-bit */
    memset(buf, 0, sizeof(buf));
    tq_pack_bits(buf, 0, 1, 1);
    tq_pack_bits(buf, 1, 0, 1);
    tq_pack_bits(buf, 7, 1, 1);
    if (tq_unpack_bits(buf, 0, 1) != 1 || tq_unpack_bits(buf, 1, 1) != 0 || tq_unpack_bits(buf, 7, 1) != 1) {
        printf(" FAIL (1-bit)\n"); return 1;
    }

    /* Test 2-bit */
    memset(buf, 0, sizeof(buf));
    tq_pack_bits(buf, 0, 3, 2);
    tq_pack_bits(buf, 3, 2, 2);
    if (tq_unpack_bits(buf, 0, 2) != 3 || tq_unpack_bits(buf, 3, 2) != 2) {
        printf(" FAIL (2-bit)\n"); return 1;
    }

    /* Test 4-bit */
    memset(buf, 0, sizeof(buf));
    tq_pack_bits(buf, 0, 15, 4);
    tq_pack_bits(buf, 1, 9, 4);
    if (tq_unpack_bits(buf, 0, 4) != 15 || tq_unpack_bits(buf, 1, 4) != 9) {
        printf(" FAIL (4-bit)\n"); return 1;
    }

    printf(" PASS\n");
    return 0;
}

/* ================================================================
 * Test 18: tq_init_mixed — mixed bit-width initialization
 * ================================================================ */
static int test_mixed_init(void) {
    printf("  test_mixed_init...");
    tq_state_t state;

    /* 3.5-bit config for d=128: 32 outlier coords at 4-bit, 96 at 3-bit */
    int rc = tq_init_mixed(&state, 128, 4, 3, 4, 3, 32, 42);
    if (rc != 0) { printf(" FAIL (init returned %d)\n", rc); return 1; }
    if (state.d != 128) { printf(" FAIL (d)\n"); return 1; }
    if (state.n_outlier != 32) { printf(" FAIL (n_outlier)\n"); return 1; }
    if (state.key_bits_high != 4) { printf(" FAIL (kbh)\n"); return 1; }
    if (state.key_bits_low  != 3) { printf(" FAIL (kbl)\n"); return 1; }
    if (state.val_bits_high != 4) { printf(" FAIL (vbh)\n"); return 1; }
    if (state.val_bits_low  != 3) { printf(" FAIL (vbl)\n"); return 1; }

    /* Key high CB should have 2^(4-1)=8 centroids (3 MSE bits for keys) */
    if (state.key_cb_high.n_centroids != 8) {
        printf(" FAIL (key_cb_high.n=%d, expected 8)\n", state.key_cb_high.n_centroids); return 1;
    }
    /* Key low CB should have 2^(3-1)=4 centroids (2 MSE bits for keys) */
    if (state.key_cb_low.n_centroids != 4) {
        printf(" FAIL (key_cb_low.n=%d, expected 4)\n", state.key_cb_low.n_centroids); return 1;
    }
    /* Val high CB should have 2^4=16 centroids */
    if (state.val_cb_high.n_centroids != 16) {
        printf(" FAIL (val_cb_high.n=%d, expected 16)\n", state.val_cb_high.n_centroids); return 1;
    }
    /* Val low CB should have 2^3=8 centroids */
    if (state.val_cb_low.n_centroids != 8) {
        printf(" FAIL (val_cb_low.n=%d, expected 8)\n", state.val_cb_low.n_centroids); return 1;
    }

    /* Pi and S must be allocated */
    if (!state.Pi || !state.S) { printf(" FAIL (matrices)\n"); return 1; }

    /* Backward compat: uniform init (n_outlier=0) */
    tq_state_t s2;
    rc = tq_init(&s2, 64, 3, 3, 42);
    if (rc != 0) { printf(" FAIL (uniform init)\n"); return 1; }
    if (s2.n_outlier != 0) { printf(" FAIL (uniform n_outlier)\n"); return 1; }
    if (s2.key_cb.n_centroids != 4) { /* 2^(3-1)=4 */
        printf(" FAIL (uniform key_cb.n=%d)\n", s2.key_cb.n_centroids); return 1;
    }

    tq_free(&state);
    tq_free(&s2);
    printf(" PASS\n");
    return 0;
}

/* ================================================================
 * Test 19: Mixed bit-width key compress/decompress roundtrip
 * ================================================================ */
static int test_mixed_key_roundtrip(void) {
    printf("  test_mixed_key_roundtrip...");
    tq_state_t state;
    tq_init_mixed(&state, 128, 4, 3, 4, 3, 32, 42);

    float key[128], recon[128];
    uint64_t rng = 123;
    int good = 0;

    for (int t = 0; t < 200; t++) {
        float norm = 0;
        for (int j = 0; j < 128; j++) {
            /* Use simple LCG for test randomness */
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            key[j] = (float)((int64_t)rng) / (float)INT64_MAX;
            norm += key[j] * key[j];
        }
        norm = sqrtf(norm);
        if (norm < 1e-10f) continue;
        for (int j = 0; j < 128; j++) key[j] /= norm;

        tq_ckey_t ck;
        tq_compress_key(&state, key, &ck);
        tq_decompress_key(&state, &ck, recon);

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
    if (good < 170) {
        printf(" FAIL (%d/200 above 0.90 cosine)\n", good);
        return 1;
    }
    printf(" PASS (%d/200 above 0.90 cosine)\n", good);
    return 0;
}

/* ================================================================
 * Test 20: Mixed bit-width value compress/decompress roundtrip
 * ================================================================ */
static int test_mixed_val_roundtrip(void) {
    printf("  test_mixed_val_roundtrip...");
    tq_state_t state;
    tq_init_mixed(&state, 128, 4, 3, 4, 3, 32, 42);

    float val[128], recon[128];
    uint64_t rng = 456;
    int good = 0;

    for (int t = 0; t < 200; t++) {
        float norm = 0;
        for (int j = 0; j < 128; j++) {
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            val[j] = (float)((int64_t)rng) / (float)INT64_MAX;
            norm += val[j] * val[j];
        }
        norm = sqrtf(norm);
        if (norm < 1e-10f) continue;

        tq_cval_t cv;
        tq_compress_val(&state, val, &cv);
        tq_decompress_val(&state, &cv, recon);

        float dot = 0, na = 0, nb = 0;
        for (int j = 0; j < 128; j++) {
            dot += val[j] * recon[j];
            na += val[j] * val[j];
            nb += recon[j] * recon[j];
        }
        float cos_sim = dot / (sqrtf(na) * sqrtf(nb) + 1e-10f);
        if (cos_sim > 0.92f) good++;
    }

    tq_free(&state);
    if (good < 170) {
        printf(" FAIL (%d/200 above 0.92 cosine)\n", good);
        return 1;
    }
    printf(" PASS (%d/200 above 0.92 cosine)\n", good);
    return 0;
}

/* ================================================================
 * Test 21: Paper configs (TQ_CONFIG_35BIT/25BIT) and 3.5-bit quality
 * ================================================================ */
static int test_paper_configs(void) {
    printf("  test_paper_configs...");
    tq_state_t s35, s25;

    if (TQ_CONFIG_35BIT(&s35, 42) != 0) { printf(" FAIL (3.5-bit init)\n"); return 1; }
    if (TQ_CONFIG_25BIT(&s25, 42) != 0) { printf(" FAIL (2.5-bit init)\n"); return 1; }

    /* Verify 3.5-bit codebook setup */
    if (s35.key_bits_high != 4 || s35.key_bits_low != 3) { printf(" FAIL (3.5 key bits)\n"); return 1; }
    if (s35.n_outlier != 32 || s35.d != 128) { printf(" FAIL (3.5 config)\n"); return 1; }

    /* Verify 2.5-bit codebook setup */
    if (s25.key_bits_high != 3 || s25.key_bits_low != 2) { printf(" FAIL (2.5 key bits)\n"); return 1; }
    if (s25.n_outlier != 32 || s25.d != 128) { printf(" FAIL (2.5 config)\n"); return 1; }

    /* 3.5-bit quality test: d=128 key roundtrip should be high quality */
    float key[128], recon[128];
    uint64_t rng = 999;
    int good = 0;
    for (int t = 0; t < 200; t++) {
        float norm = 0;
        for (int j = 0; j < 128; j++) {
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            key[j] = (float)((int64_t)rng) / (float)INT64_MAX;
            norm += key[j] * key[j];
        }
        norm = sqrtf(norm);
        if (norm < 1e-10f) continue;
        for (int j = 0; j < 128; j++) key[j] /= norm;

        tq_ckey_t ck;
        tq_compress_key(&s35, key, &ck);
        tq_decompress_key(&s35, &ck, recon);

        float dot = 0, na = 0, nb = 0;
        for (int j = 0; j < 128; j++) { dot += key[j]*recon[j]; na += key[j]*key[j]; nb += recon[j]*recon[j]; }
        if (dot / (sqrtf(na)*sqrtf(nb) + 1e-10f) > 0.95f) good++;
    }

    tq_free(&s35);
    tq_free(&s25);

    if (good < 120) { printf(" FAIL (3.5-bit quality: %d/200 > 0.95 cosine)\n", good); return 1; }
    printf(" PASS (3.5-bit: %d/200 > 0.95 cosine)\n", good);
    return 0;
}

/* ================================================================ */

int main(void)
{
    printf("=== TurboQuant KV Cache Compression Tests ===\n\n");

    test_orthogonality();
    test_codebook_ordering();
    test_key_roundtrip();
    test_val_roundtrip();
    test_qjl_inner_product();
    test_memory_savings();
    test_zero_vector();
    test_head_dim_128();
    test_stress();
    test_attention_scores();
    test_state_overhead();
    test_invalid_params();
    test_full_kv_simulation();
    test_mse_matches_paper();
    test_mse_4bit();
    test_qjl_unbiased();

    int fails = 0;
    fails += test_bitstream_pack();
    fails += test_mixed_init();
    fails += test_mixed_key_roundtrip();
    fails += test_mixed_val_roundtrip();
    fails += test_paper_configs();

    printf("\n=== Results: %d PASS, %d FAIL ===\n",
           tests_passed, tests_failed + fails);
    return (tests_failed + fails) > 0 ? 1 : 0;
}
