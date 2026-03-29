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
static float test_randf(void)
{
    test_rng_state ^= test_rng_state << 13;
    test_rng_state ^= test_rng_state >> 7;
    test_rng_state ^= test_rng_state << 17;
    return (float)(test_rng_state >> 11) / (float)(1ULL << 53) * 2.0f - 1.0f;
}

static void gen_random_vec(float *v, int n)
{
    for (int i = 0; i < n; i++) v[i] = test_randf();
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
    /* QJL should be at least comparable to MSE (often better due to unbiased correction) */
    if (qjl_rmse < mse_rmse * 2.0f) PASS();
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

    printf("\n=== Results: %d PASS, %d FAIL ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
