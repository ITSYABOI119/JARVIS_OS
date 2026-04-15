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

/* ---- AVX2 horizontal sum helper ---- */

static inline float hsum_avx2(__m256 v)
{
    __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 s  = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    return _mm_cvtss_f32(s);
}

/* ---- AVX2 fused Q4_K dequant-dot kernel ----
 *
 * Reads Q4_K blocks directly, dequantizes into SIMD registers, and
 * accumulates dot product with F32 activation vector — no intermediate
 * buffer.  Produces bit-identical results (modulo FP reordering) to the
 * scalar dequant_q4_k + manual dot path.
 *
 * Block layout recap (144 bytes, 256 values):
 *   d(f16) + dmin(f16) + scales[12] + qs[128]
 *   4 groups of 64 values each.  Per group j:
 *     lower nibbles of qs[32j..32j+31] → 32 values at [64j..64j+31], scale d1, min m1
 *     upper nibbles of qs[32j..32j+31] → 32 values at [64j+32..64j+63], scale d2, min m2
 */
static float qdot_q4_k_avx2(const void *row_data, const float *x, int K)
{
    const int n_blocks = K / 256;
    const q4_k_block_t *blocks = (const q4_k_block_t *)row_data;
    const __m256i mask_0f = _mm256_set1_epi32(0x0F);

    __m256 acc = _mm256_setzero_ps();

    for (int b = 0; b < n_blocks; b++) {
        const q4_k_block_t *blk = &blocks[b];
        float d    = f16_to_f32(blk->d);
        float dmin = f16_to_f32(blk->dmin);
        const uint8_t *qs = blk->qs;
        const float *xb = x + b * 256;

        /* Unpack 6-bit scales and mins from 12 packed bytes — same as reference */
        uint8_t sc[8], mn[8];
        for (int i = 0; i < 4; i++) {
            sc[i]     = blk->scales[i] & 63;
            sc[i + 4] = (blk->scales[i + 8] & 0xF) | ((blk->scales[i] >> 6) << 4);
            mn[i]     = blk->scales[i + 4] & 63;
            mn[i + 4] = (blk->scales[i + 8] >> 4) | ((blk->scales[i + 4] >> 6) << 4);
        }

        /* 4 groups of 64 values */
        for (int j = 0; j < 4; j++) {
            __m256 vd1 = _mm256_set1_ps(d * sc[2 * j]);
            __m256 vm1 = _mm256_set1_ps(dmin * mn[2 * j]);
            __m256 vd2 = _mm256_set1_ps(d * sc[2 * j + 1]);
            __m256 vm2 = _mm256_set1_ps(dmin * mn[2 * j + 1]);

            /* Process 32 bytes in chunks of 8 */
            for (int k = 0; k < 32; k += 8) {
                /* Load 8 quantized bytes, zero-extend to 8 x int32 */
                __m128i raw8 = _mm_loadl_epi64((const __m128i *)(qs + 32 * j + k));
                __m256i raw32 = _mm256_cvtepu8_epi32(raw8);

                /* Lower nibble: 8 values at x[64*j + k .. k+7] */
                __m256i lo_i = _mm256_and_si256(raw32, mask_0f);
                __m256 flo   = _mm256_cvtepi32_ps(lo_i);
                __m256 x_lo  = _mm256_loadu_ps(xb + 64 * j + k);
                __m256 val_lo = _mm256_fmsub_ps(vd1, flo, vm1);   /* d1*nibble - m1 */
                acc = _mm256_fmadd_ps(val_lo, x_lo, acc);         /* accumulate */

                /* Upper nibble: 8 values at x[64*j + 32 + k .. k+7] */
                __m256i hi_i = _mm256_srli_epi32(raw32, 4);
                __m256 fhi   = _mm256_cvtepi32_ps(hi_i);
                __m256 x_hi  = _mm256_loadu_ps(xb + 64 * j + 32 + k);
                __m256 val_hi = _mm256_fmsub_ps(vd2, fhi, vm2);   /* d2*nibble - m2 */
                acc = _mm256_fmadd_ps(val_hi, x_hi, acc);         /* accumulate */
            }
        }
    }

    return hsum_avx2(acc);
}

#endif /* __AVX2__ */

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
#ifdef __AVX2__
    if (type == GGML_TYPE_Q4_K)
        return qdot_q4_k_avx2(row_data, x, K);
#endif
    return qdot_row_scalar(row_data, x, K, type);
}
