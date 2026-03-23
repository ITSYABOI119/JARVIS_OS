/*
 * JARVIS AI-OS Phase 3 — Pure C Tensor Operations
 * Minimal ops for transformer inference on seL4 bare metal.
 * Replaces C++ ggml-cpu/ops.cpp with portable C implementations.
 */

#ifndef TENSOR_OPS_H
#define TENSOR_OPS_H

#include <stdint.h>

/* Element-wise operations */
void tensor_add(const float *a, const float *b, float *out, int n);
void tensor_mul(const float *a, const float *b, float *out, int n);
void tensor_scale(const float *a, float s, float *out, int n);

/* Matrix multiply: out[M*N] = a[M*K] * b[K*N] (row-major) */
void tensor_matmul(const float *a, const float *b, float *out, int M, int K, int N);

/* Activation functions */
void tensor_relu(const float *in, float *out, int n);
void tensor_gelu(const float *in, float *out, int n);
void tensor_silu(const float *in, float *out, int n);

/* Normalization */
void tensor_rms_norm(const float *in, const float *weight, float *out, int n, float eps);
void tensor_softmax(const float *in, float *out, int n);

/* Positional encoding */
void tensor_rope(float *q, float *k, int dim, int head_dim, int pos);

#endif
