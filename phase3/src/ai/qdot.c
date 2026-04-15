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

/* ---- AVX2 fused Q6_K dequant-dot kernel ----
 *
 * Reads Q6_K blocks directly, dequantizes into SIMD registers, and
 * accumulates dot product with F32 activation vector — no intermediate
 * buffer.
 *
 * Block layout (210 bytes, 256 values):
 *   ql[128] + qh[64] + scales[16] + d(f16)
 *
 * Two halves (n=0, n=128). Within each half, 32 iterations produce
 * 4 values each (q1,q2,q3,q4) from interleaved ql/qh bits.
 * Scale index: is = n/16 + l/16, used as sc[is+0], sc[is+2], sc[is+4], sc[is+6].
 *
 * AVX2 approach: process 8 l-values at a time. Within each group of 8,
 * the scale index is constant when the group stays inside one 16-value
 * sub-block (l mod 16 aligned). Groups at l=0,8 share is=n/16+0;
 * groups at l=16,24 share is=n/16+1.
 */
static float qdot_q6_k_avx2(const void *row_data, const float *x, int K)
{
    const int n_blocks = K / 256;
    const q6_k_block_t *blocks = (const q6_k_block_t *)row_data;
    const __m256i mask_0f = _mm256_set1_epi32(0x0F);
    const __m256i mask_03 = _mm256_set1_epi32(0x03);
    const __m256i sub_32  = _mm256_set1_epi32(32);

    __m256 acc = _mm256_setzero_ps();

    for (int b = 0; b < n_blocks; b++) {
        const q6_k_block_t *blk = &blocks[b];
        float d = f16_to_f32(blk->d);
        const uint8_t *ql = blk->ql;
        const uint8_t *qh = blk->qh;
        const int8_t  *sc = blk->scales;
        const float *xb = x + b * 256;

        for (int n = 0; n < 256; n += 128) {
            for (int l = 0; l < 32; l += 8) {
                int is = n / 16 + l / 16;

                /* Load 8 bytes from ql[n/2+l] and ql[n/2+l+32],
                 * zero-extend each byte to int32 */
                __m128i ql1_8  = _mm_loadl_epi64((const __m128i *)(ql + n/2 + l));
                __m128i ql2_8  = _mm_loadl_epi64((const __m128i *)(ql + n/2 + l + 32));
                __m256i ql1_32 = _mm256_cvtepu8_epi32(ql1_8);
                __m256i ql2_32 = _mm256_cvtepu8_epi32(ql2_8);

                /* Load 8 bytes from qh[n/4+l], zero-extend to int32 */
                __m128i qh_8  = _mm_loadl_epi64((const __m128i *)(qh + n/4 + l));
                __m256i qh_32 = _mm256_cvtepu8_epi32(qh_8);

                /* q1: low nibble of ql1 | (qh bits 0-1) << 4, minus 32 */
                __m256i lo1 = _mm256_and_si256(ql1_32, mask_0f);
                __m256i h1  = _mm256_and_si256(qh_32, mask_03);
                __m256i q1  = _mm256_or_si256(lo1, _mm256_slli_epi32(h1, 4));
                q1 = _mm256_sub_epi32(q1, sub_32);

                /* q2: low nibble of ql2 | (qh bits 2-3) << 4, minus 32 */
                __m256i lo2 = _mm256_and_si256(ql2_32, mask_0f);
                __m256i h2  = _mm256_and_si256(_mm256_srli_epi32(qh_32, 2), mask_03);
                __m256i q2  = _mm256_or_si256(lo2, _mm256_slli_epi32(h2, 4));
                q2 = _mm256_sub_epi32(q2, sub_32);

                /* q3: high nibble of ql1 | (qh bits 4-5) << 4, minus 32 */
                __m256i hi1 = _mm256_srli_epi32(ql1_32, 4);
                __m256i h3  = _mm256_and_si256(_mm256_srli_epi32(qh_32, 4), mask_03);
                __m256i q3  = _mm256_or_si256(hi1, _mm256_slli_epi32(h3, 4));
                q3 = _mm256_sub_epi32(q3, sub_32);

                /* q4: high nibble of ql2 | (qh bits 6-7) << 4, minus 32 */
                __m256i hi2 = _mm256_srli_epi32(ql2_32, 4);
                __m256i h4  = _mm256_srli_epi32(qh_32, 6);  /* only 2 bits remain */
                __m256i q4  = _mm256_or_si256(hi2, _mm256_slli_epi32(h4, 4));
                q4 = _mm256_sub_epi32(q4, sub_32);

                /* Convert to float, multiply by d * scale, FMA with x */
                __m256 fq1 = _mm256_cvtepi32_ps(q1);
                __m256 fq2 = _mm256_cvtepi32_ps(q2);
                __m256 fq3 = _mm256_cvtepi32_ps(q3);
                __m256 fq4 = _mm256_cvtepi32_ps(q4);

                __m256 ds1 = _mm256_set1_ps(d * sc[is + 0]);
                __m256 ds2 = _mm256_set1_ps(d * sc[is + 2]);
                __m256 ds3 = _mm256_set1_ps(d * sc[is + 4]);
                __m256 ds4 = _mm256_set1_ps(d * sc[is + 6]);

                __m256 x1 = _mm256_loadu_ps(xb + n + l);
                __m256 x2 = _mm256_loadu_ps(xb + n + l + 32);
                __m256 x3 = _mm256_loadu_ps(xb + n + l + 64);
                __m256 x4 = _mm256_loadu_ps(xb + n + l + 96);

                acc = _mm256_fmadd_ps(_mm256_mul_ps(ds1, fq1), x1, acc);
                acc = _mm256_fmadd_ps(_mm256_mul_ps(ds2, fq2), x2, acc);
                acc = _mm256_fmadd_ps(_mm256_mul_ps(ds3, fq3), x3, acc);
                acc = _mm256_fmadd_ps(_mm256_mul_ps(ds4, fq4), x4, acc);
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
    if (type == GGML_TYPE_Q6_K)
        return qdot_q6_k_avx2(row_data, x, K);
#endif
    return qdot_row_scalar(row_data, x, K, type);
}
