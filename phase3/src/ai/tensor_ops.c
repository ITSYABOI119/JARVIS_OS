/*
 * JARVIS AI-OS Phase 3 — Pure C Tensor Operations
 *
 * Naive implementations — correctness first.
 * TODO: AVX2/FMA SIMD optimization for Ryzen 5 5600.
 */

#include "tensor_ops.h"
#include <math.h>
#include <string.h>

void tensor_add(const float *a, const float *b, float *out, int n)
{
    for (int i = 0; i < n; i++)
        out[i] = a[i] + b[i];
}

void tensor_mul(const float *a, const float *b, float *out, int n)
{
    for (int i = 0; i < n; i++)
        out[i] = a[i] * b[i];
}

void tensor_scale(const float *a, float s, float *out, int n)
{
    for (int i = 0; i < n; i++)
        out[i] = a[i] * s;
}

void tensor_matmul(const float *a, const float *b, float *out, int M, int K, int N)
{
    /* N=1 fast path: matrix-vector multiply (all inference matmuls) */
    if (N == 1) {
        for (int i = 0; i < M; i++) {
            const float *row = a + (size_t)i * K;
            float sum = 0.0f;
            int k = 0;
            /* 4-wide loop unroll for better ILP */
            for (; k + 3 < K; k += 4)
                sum += row[k]*b[k] + row[k+1]*b[k+1] + row[k+2]*b[k+2] + row[k+3]*b[k+3];
            for (; k < K; k++)
                sum += row[k] * b[k];
            out[i] = sum;
        }
        return;
    }

    /* General case: tiled matmul for cache locality */
    memset(out, 0, (size_t)M * (size_t)N * sizeof(float));
    const int TILE = 32;
    for (int i0 = 0; i0 < M; i0 += TILE)
        for (int k0 = 0; k0 < K; k0 += TILE)
            for (int j0 = 0; j0 < N; j0 += TILE) {
                int imax = i0 + TILE < M ? i0 + TILE : M;
                int kmax = k0 + TILE < K ? k0 + TILE : K;
                int jmax = j0 + TILE < N ? j0 + TILE : N;
                for (int i = i0; i < imax; i++)
                    for (int k = k0; k < kmax; k++) {
                        float aik = a[(size_t)i * K + k];
                        for (int j = j0; j < jmax; j++)
                            out[(size_t)i * N + j] += aik * b[(size_t)k * N + j];
                    }
            }
}

void tensor_relu(const float *in, float *out, int n)
{
    for (int i = 0; i < n; i++)
        out[i] = in[i] > 0.0f ? in[i] : 0.0f;
}

void tensor_gelu(const float *in, float *out, int n)
{
    /* GELU(x) = 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3))) */
    const float c = 0.7978845608f; /* sqrt(2/pi) */
    for (int i = 0; i < n; i++) {
        float x = in[i];
        float inner = c * (x + 0.044715f * x * x * x);
        out[i] = 0.5f * x * (1.0f + tanhf(inner));
    }
}

void tensor_silu(const float *in, float *out, int n)
{
    /* SiLU(x) = x * sigmoid(x) = x / (1 + exp(-x)) */
    for (int i = 0; i < n; i++) {
        float x = in[i];
        out[i] = x / (1.0f + expf(-x));
    }
}

void tensor_rms_norm(const float *in, const float *weight, float *out, int n, float eps)
{
    /* RMS = sqrt(mean(x^2) + eps); out = (in / RMS) * weight */
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++)
        sum_sq += in[i] * in[i];
    float rms = sqrtf(sum_sq / (float)n + eps);
    float inv_rms = 1.0f / rms;
    for (int i = 0; i < n; i++)
        out[i] = in[i] * inv_rms * weight[i];
}

void tensor_softmax(const float *in, float *out, int n)
{
    /* Numerically stable: subtract max first */
    float max_val = in[0];
    for (int i = 1; i < n; i++)
        if (in[i] > max_val) max_val = in[i];

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        out[i] = expf(in[i] - max_val);
        sum += out[i];
    }
    float inv_sum = 1.0f / sum;
    for (int i = 0; i < n; i++)
        out[i] *= inv_sum;
}

void tensor_rope(float *q, float *k, int dim, int head_dim, int pos)
{
    /* Rotary Position Embedding — apply rotation to each pair of elements */
    int half = head_dim / 2;
    for (int i = 0; i < dim / 2; i++) {
        int idx = i % half;
        float freq = 1.0f / powf(10000.0f, (float)(2 * idx) / (float)head_dim);
        float angle = (float)pos * freq;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);

        float q0 = q[2 * i], q1 = q[2 * i + 1];
        q[2 * i]     = q0 * cos_a - q1 * sin_a;
        q[2 * i + 1] = q0 * sin_a + q1 * cos_a;

        float k0 = k[2 * i], k1 = k[2 * i + 1];
        k[2 * i]     = k0 * cos_a - k1 * sin_a;
        k[2 * i + 1] = k0 * sin_a + k1 * cos_a;
    }
}
