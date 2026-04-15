/*
 * JARVIS AI-OS Phase 3 — Fused Quantized Dot Product
 *
 * Type-specific AVX2 kernels: dequant + dot in one SIMD pass.
 * No intermediate F32 buffer. Scalar fallback for CI (no AVX2).
 *
 * Pure C11. AVX2 path guarded by #ifdef __AVX2__.
 */

#ifndef QDOT_H
#define QDOT_H

#include "dequant.h"  /* ggml_type_t, block structs, f16_to_f32 */

/**
 * Fused quantized dot product: dot(dequant(W_row), x) without intermediate buffer.
 *
 * @param row_data  Pointer to one quantized row (raw bytes from GGUF .rodata)
 * @param x         F32 activation vector [K]
 * @param K         Row length (number of elements, must be multiple of block_size)
 * @param type      Quantization type (GGML_TYPE_Q4_K, etc.)
 * @return          Dot product as F32
 */
float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type);

#endif /* QDOT_H */
