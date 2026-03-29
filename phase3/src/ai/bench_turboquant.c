/*
 * JARVIS AI-OS Phase 3 -- TurboQuant KV Cache Benchmark
 *
 * Measures: compression ratio, quality (cosine sim, attention correlation),
 * throughput (compress/decompress ops/sec), and memory savings for
 * Llama 3.2 1B and 3B model dimensions.
 */

#define _POSIX_C_SOURCE 199309L
#include "turboquant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ---- Helpers ---- */

static uint64_t bench_rng = 98765ULL;
static float randf(void)
{
    bench_rng ^= bench_rng << 13;
    bench_rng ^= bench_rng >> 7;
    bench_rng ^= bench_rng << 17;
    return (float)(bench_rng >> 11) / (float)(1ULL << 53) * 2.0f - 1.0f;
}

static void gen_vec(float *v, int n)
{
    for (int i = 0; i < n; i++) v[i] = randf();
}

static float cosine_sim(const float *a, const float *b, int n)
{
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i];
    }
    if (na < 1e-20f || nb < 1e-20f) return 0;
    return dot / (sqrtf(na) * sqrtf(nb));
}

static double elapsed_ms(struct timespec *start, struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1e6;
}

/* ---- Benchmark one configuration ---- */

typedef struct {
    const char *name;
    int head_dim;
    int n_kv_heads;
    int n_layers;
    int key_bits;
    int val_bits;
} bench_config_t;

static void run_benchmark(const bench_config_t *cfg)
{
    int seq_len = 512;
    int d = cfg->head_dim;

    printf("--- %s (d=%d, heads=%d, layers=%d, key=%d-bit, val=%d-bit) ---\n",
           cfg->name, d, cfg->n_kv_heads, cfg->n_layers, cfg->key_bits, cfg->val_bits);

    tq_state_t state;
    if (tq_init(&state, d, cfg->key_bits, cfg->val_bits, 42) != 0) {
        printf("  ERROR: init failed\n\n");
        return;
    }

    /* Memory calculation */
    size_t tq_per_pos = tq_compressed_kv_bytes(&state, cfg->n_kv_heads);
    size_t f32_per_pos = tq_baseline_kv_bytes(d, cfg->n_kv_heads);
    size_t tq_total = (size_t)cfg->n_layers * (size_t)seq_len * tq_per_pos +
                      (size_t)cfg->n_layers * tq_state_bytes(&state);
    size_t f32_total = (size_t)cfg->n_layers * (size_t)seq_len * f32_per_pos;

    printf("  KV cache (F32):    %8.2f MB  (%zu bytes/pos/layer)\n",
           (double)f32_total / (1024.0 * 1024.0), f32_per_pos);
    printf("  KV cache (TQ):     %8.2f MB  (%zu bytes/pos/layer)\n",
           (double)tq_total / (1024.0 * 1024.0), tq_per_pos);
    printf("  Compression:       %8.1fx\n", (double)f32_total / (double)tq_total);
    printf("  Savings:           %8.2f MB\n",
           (double)(f32_total - tq_total) / (1024.0 * 1024.0));

    /* Quality measurement: compress/decompress 1000 vectors */
    int n_samples = 1000;
    double key_cos_sum = 0, val_cos_sum = 0;
    double key_dot_err_sum = 0;
    int n_dot_tests = 0;

    for (int i = 0; i < n_samples; i++) {
        float key[128], val[128], krecon[128], vrecon[128];
        gen_vec(key, d);
        gen_vec(val, d);

        tq_ckey_t ck;
        tq_cval_t cv;
        tq_compress_key(&state, key, &ck);
        tq_compress_val(&state, val, &cv);
        tq_decompress_key(&state, &ck, krecon);
        tq_decompress_val(&state, &cv, vrecon);

        key_cos_sum += cosine_sim(key, krecon, d);
        val_cos_sum += cosine_sim(val, vrecon, d);

        /* Attention dot product accuracy */
        float query[128];
        gen_vec(query, d);
        float true_dot = 0, qjl_dot;
        for (int j = 0; j < d; j++) true_dot += query[j] * key[j];
        qjl_dot = tq_dot_key(&state, query, &ck);
        key_dot_err_sum += (qjl_dot - true_dot) * (qjl_dot - true_dot);
        n_dot_tests++;
    }

    printf("  Key cosine sim:    %8.4f  (avg over %d vectors)\n",
           key_cos_sum / n_samples, n_samples);
    printf("  Val cosine sim:    %8.4f\n", val_cos_sum / n_samples);
    printf("  QJL dot RMSE:      %8.4f\n", sqrt(key_dot_err_sum / n_dot_tests));

    /* Throughput: time compress + decompress */
    int throughput_iters = 5000;
    float key_buf[128], val_buf[128], recon[128];
    gen_vec(key_buf, d);
    gen_vec(val_buf, d);
    tq_ckey_t ck;
    tq_cval_t cv;

    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < throughput_iters; i++) {
        tq_compress_key(&state, key_buf, &ck);
        tq_decompress_key(&state, &ck, recon);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double key_ms = elapsed_ms(&t0, &t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < throughput_iters; i++) {
        tq_compress_val(&state, val_buf, &cv);
        tq_decompress_val(&state, &cv, recon);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double val_ms = elapsed_ms(&t0, &t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < throughput_iters; i++) {
        float query[128];
        gen_vec(query, d);
        tq_dot_key(&state, query, &ck);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dot_ms = elapsed_ms(&t0, &t1);

    printf("  Key compress+decompress: %.1f us/op  (%.0f Kops/s)\n",
           key_ms * 1000.0 / throughput_iters,
           throughput_iters / (key_ms / 1000.0) / 1000.0);
    printf("  Val compress+decompress: %.1f us/op  (%.0f Kops/s)\n",
           val_ms * 1000.0 / throughput_iters,
           throughput_iters / (val_ms / 1000.0) / 1000.0);
    printf("  QJL dot product:         %.1f us/op  (%.0f Kops/s)\n",
           dot_ms * 1000.0 / throughput_iters,
           throughput_iters / (dot_ms / 1000.0) / 1000.0);

    printf("\n");
    tq_free(&state);
}

int main(void)
{
    printf("========================================\n");
    printf("  TurboQuant KV Cache Benchmark\n");
    printf("  Date: 2026-03-29\n");
    printf("========================================\n\n");

    bench_config_t configs[] = {
        { "Llama 3.2 1B (3-bit)", 64, 8, 16, 3, 3 },
        { "Llama 3.2 3B (3-bit)", 128, 8, 28, 3, 3 },
        { "Llama 3.2 1B (2-bit)", 64, 8, 16, 2, 2 },
        { "Llama 3.2 1B (4-bit)", 64, 8, 16, 4, 4 },
    };

    for (int i = 0; i < 4; i++)
        run_benchmark(&configs[i]);

    printf("========================================\n");
    printf("  Benchmark complete.\n");
    printf("========================================\n");
    return 0;
}
