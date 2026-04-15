/*
 * JARVIS AI-OS Phase 3 — Fused Quantized Dot Product
 *
 * AVX2 fused kernels with scalar fallback.
 * Each kernel reads quantized data directly into SIMD registers,
 * multiplies by scale, and FMA with F32 activation — no tmp buffer.
 */

#include "qdot.h"
#include <string.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

/* ---- Scalar fallback: dequant + dot (uses existing dequant_row) ---- */

static float qdot_row_scalar(const void *row_data, const float *x, int K, ggml_type_t type)
{
    const int bs = dequant_type_block_size(type);
    const size_t bb = dequant_type_block_bytes(type);
    if (bs <= 0 || bb == 0) return 0.0f;

    const int n_blocks = K / bs;
    const uint8_t *ptr = (const uint8_t *)row_data;
    float tmp[256];
    float dot = 0.0f;

    for (int b = 0; b < n_blocks; b++) {
        dequant_row(ptr, tmp, bs, type);
        const float *xb = x + b * bs;
        for (int j = 0; j < bs; j++)
            dot += tmp[j] * xb[j];
        ptr += bb;
    }
    return dot;
}

/* ---- Dispatch ---- */

float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type)
{
    /* AVX2 kernels added in subsequent tasks */
    return qdot_row_scalar(row_data, x, K, type);
}
