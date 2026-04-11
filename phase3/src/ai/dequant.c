/*
 * JARVIS AI-OS Phase 3 — GGUF Dequantization Routines
 *
 * Converts quantized GGUF tensor data (Q4_0, Q8_0, F16) to float32.
 * Pure C11, no intrinsics, no external dependencies.
 *
 * Q4_0: 32 values per block, 18 bytes (f16 scale + 16 bytes of nibbles)
 * Q8_0: 32 values per block, 34 bytes (f16 scale + 32 int8 values)
 * F16:  1 value per element, 2 bytes each
 * F32:  passthrough, 4 bytes each
 */

#include "dequant.h"
#include <string.h>  /* memcpy */

/* ---- F16 to F32 conversion ---- */

float f16_to_f32(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;

    if (exp == 0) {
        if (mant == 0) {
            /* Signed zero */
            union { uint32_t u; float f; } u = { sign };
            return u.f;
        }
        /* Denormalized: normalize by shifting mantissa up */
        exp = 1;
        while (!(mant & 0x400)) {
            mant <<= 1;
            exp--;
        }
        mant &= 0x3FF;
        exp += (127 - 15);
    } else if (exp == 31) {
        /* Infinity or NaN */
        union { uint32_t u; float f; } u = { sign | 0x7F800000 | (mant << 13) };
        return u.f;
    } else {
        /* Normalized */
        exp += (127 - 15);
    }

    union { uint32_t u; float f; } u = { sign | (exp << 23) | (mant << 13) };
    return u.f;
}

/* ---- Q4_0 dequantization ---- */

static void dequant_q4_0(const void *src, float *dst, int n_blocks)
{
    const q4_0_block_t *blocks = (const q4_0_block_t *)src;
    for (int b = 0; b < n_blocks; b++) {
        float scale = f16_to_f32(blocks[b].d);
        for (int j = 0; j < 16; j++) {
            uint8_t byte = blocks[b].qs[j];
            dst[b * 32 + j]      = (float)((byte & 0x0F) - 8) * scale;
            dst[b * 32 + j + 16] = (float)((byte >> 4)   - 8) * scale;
        }
    }
}

/* ---- Q8_0 dequantization ---- */

static void dequant_q8_0(const void *src, float *dst, int n_blocks)
{
    const q8_0_block_t *blocks = (const q8_0_block_t *)src;
    for (int b = 0; b < n_blocks; b++) {
        float scale = f16_to_f32(blocks[b].d);
        for (int j = 0; j < 32; j++)
            dst[b * 32 + j] = (float)blocks[b].qs[j] * scale;
    }
}

/* ---- F16 dequantization ---- */

static void dequant_f16(const void *src, float *dst, int n_values)
{
    const uint16_t *f16 = (const uint16_t *)src;
    for (int i = 0; i < n_values; i++)
        dst[i] = f16_to_f32(f16[i]);
}

/* ---- BF16 dequantization ----
 * bfloat16 = upper 16 bits of F32. Left-shift by 16 to reconstruct F32. */
static void dequant_bf16(const void *src, float *dst, int n_values)
{
    const uint16_t *bf = (const uint16_t *)src;
    for (int i = 0; i < n_values; i++) {
        union { uint32_t u; float f; } un;
        un.u = ((uint32_t)bf[i]) << 16;
        dst[i] = un.f;
    }
}

/* ---- Q4_K dequantization (256 values per block, 144 bytes) ----
 *
 * Matches ggml's dequantize_row_q4_K exactly. 128 qs bytes encode
 * 256 nibbles in an interleaved layout: 4 groups of 64 values each.
 * Within each group: lower nibbles of 32 bytes → first 32 values,
 * upper nibbles of same 32 bytes → next 32 values, using DIFFERENT scales.
 */
static void dequant_q4_k(const void *src, float *dst, int n_blocks)
{
    const q4_k_block_t *blocks = (const q4_k_block_t *)src;
    for (int b = 0; b < n_blocks; b++) {
        const q4_k_block_t *blk = &blocks[b];
        float d    = f16_to_f32(blk->d);
        float dmin = f16_to_f32(blk->dmin);
        const uint8_t *q = blk->qs;

        /* Unpack 6-bit scales and mins from 12 packed bytes */
        uint8_t sc[8], mn[8];
        for (int i = 0; i < 4; i++) {
            sc[i]     = blk->scales[i] & 63;
            sc[i + 4] = (blk->scales[i + 8] & 0xF) | ((blk->scales[i] >> 6) << 4);
            mn[i]     = blk->scales[i + 4] & 63;
            mn[i + 4] = (blk->scales[i + 8] >> 4) | ((blk->scales[i + 4] >> 6) << 4);
        }

        float *y = dst + b * 256;

        /* 4 groups of 64 values: lower nibbles → first 32, upper → next 32 */
        for (int j = 0; j < 4; j++) {
            float d1 = d * sc[2 * j];
            float d2 = d * sc[2 * j + 1];
            float m1 = dmin * mn[2 * j];
            float m2 = dmin * mn[2 * j + 1];
            for (int l = 0; l < 32; l++) {
                y[64 * j + l]      = d1 * (q[32 * j + l] & 0xF) - m1;
                y[64 * j + l + 32] = d2 * (q[32 * j + l] >> 4)  - m2;
            }
        }
    }
}

/* ---- Q5_K dequantization (256 values per block, 176 bytes) ----
 *
 * Same scale/min packing as Q4_K, but values are 5-bit (not 4-bit).
 * Low 4 bits from qs[] (nibbles), high 1 bit from qh[] (bit mask).
 * Value range: 0-31 (vs 0-15 for Q4_K).
 *
 * qh layout: 32 bytes = 256 bits. For each group of 64 values,
 * 2 bits of qh are used per byte (shifted by u1/u2 which advance <<2).
 * Group j (0-3): u1=1<<(2j), u2=1<<(2j+1) select the high bit.
 */
static void dequant_q5_k(const void *src, float *dst, int n_blocks)
{
    const q5_k_block_t *blocks = (const q5_k_block_t *)src;
    for (int b = 0; b < n_blocks; b++) {
        const q5_k_block_t *blk = &blocks[b];
        float d    = f16_to_f32(blk->d);
        float dmin = f16_to_f32(blk->dmin);
        const uint8_t *q  = blk->qs;
        const uint8_t *qh = blk->qh;

        /* Unpack 6-bit scales and mins from 12 packed bytes (same as Q4_K) */
        uint8_t sc[8], mn[8];
        for (int i = 0; i < 4; i++) {
            sc[i]     = blk->scales[i] & 63;
            sc[i + 4] = (blk->scales[i + 8] & 0xF) | ((blk->scales[i] >> 6) << 4);
            mn[i]     = blk->scales[i + 4] & 63;
            mn[i + 4] = (blk->scales[i + 8] >> 4) | ((blk->scales[i + 4] >> 6) << 4);
        }

        float *y = dst + b * 256;

        /* 4 groups of 64 values, each split into two sub-blocks of 32 */
        uint8_t u1 = 1, u2 = 2;
        for (int j = 0; j < 4; j++) {
            float d1 = d * sc[2 * j];
            float d2 = d * sc[2 * j + 1];
            float m1 = dmin * mn[2 * j];
            float m2 = dmin * mn[2 * j + 1];
            for (int l = 0; l < 32; l++) {
                uint8_t lo1 = q[32 * j + l] & 0xF;
                uint8_t hi1 = (qh[l] & u1) ? 16 : 0;
                y[64 * j + l]      = d1 * (lo1 + hi1) - m1;

                uint8_t lo2 = q[32 * j + l] >> 4;
                uint8_t hi2 = (qh[l] & u2) ? 16 : 0;
                y[64 * j + l + 32] = d2 * (lo2 + hi2) - m2;
            }
            u1 <<= 2;
            u2 <<= 2;
        }
    }
}

/* ---- Q6_K dequantization (256 values per block, 210 bytes) ----
 *
 * Matches ggml's dequantize_row_q6_K exactly. The ql/qh/scales layout
 * uses a complex interleaving pattern — NOT a simple linear mapping.
 *
 * Each block of 256 values is processed in two halves of 128.
 * Within each half, 32 iterations produce 4 output values each:
 *   y[n+l],  y[n+l+32],  y[n+l+64],  y[n+l+96]
 * using ql lower nibbles, ql upper nibbles, and qh 2-bit pairs.
 */
static void dequant_q6_k(const void *src, float *dst, int n_blocks)
{
    const q6_k_block_t *blocks = (const q6_k_block_t *)src;
    for (int b = 0; b < n_blocks; b++) {
        const q6_k_block_t *blk = &blocks[b];
        float d = f16_to_f32(blk->d);
        const uint8_t *ql = blk->ql;
        const uint8_t *qh = blk->qh;
        const int8_t  *sc = blk->scales;
        float *y = dst + b * 256;

        for (int n = 0; n < 256; n += 128) {
            for (int l = 0; l < 32; ++l) {
                int is = n / 16 + l / 16;
                int8_t q1 = (int8_t)((ql[n/2+l]    & 0xF) | (((qh[n/4+l] >> 0) & 3) << 4)) - 32;
                int8_t q2 = (int8_t)((ql[n/2+l+32]  & 0xF) | (((qh[n/4+l] >> 2) & 3) << 4)) - 32;
                int8_t q3 = (int8_t)((ql[n/2+l]     >> 4)  | (((qh[n/4+l] >> 4) & 3) << 4)) - 32;
                int8_t q4 = (int8_t)((ql[n/2+l+32]  >> 4)  | (((qh[n/4+l] >> 6) & 3) << 4)) - 32;
                y[n+l+ 0] = d * sc[is+0] * q1;
                y[n+l+32] = d * sc[is+2] * q2;
                y[n+l+64] = d * sc[is+4] * q3;
                y[n+l+96] = d * sc[is+6] * q4;
            }
        }
    }
}

/* ---- F32 passthrough ---- */

static void dequant_f32(const void *src, float *dst, int n_values)
{
    memcpy(dst, src, (size_t)n_values * sizeof(float));
}

/* ---- Type info helpers ---- */

size_t dequant_type_block_bytes(ggml_type_t type)
{
    switch (type) {
    case GGML_TYPE_Q4_0: return sizeof(q4_0_block_t);  /* 18 */
    case GGML_TYPE_Q8_0: return sizeof(q8_0_block_t);  /* 34 */
    case GGML_TYPE_Q4_K: return sizeof(q4_k_block_t);  /* 144 */
    case GGML_TYPE_Q5_K: return sizeof(q5_k_block_t);  /* 176 */
    case GGML_TYPE_Q6_K: return sizeof(q6_k_block_t);  /* 210 */
    case GGML_TYPE_F16:  return 2;
    case GGML_TYPE_BF16: return 2;
    case GGML_TYPE_F32:  return 4;
    default:             return 0;
    }
}

int dequant_type_block_size(ggml_type_t type)
{
    switch (type) {
    case GGML_TYPE_Q4_0: return 32;
    case GGML_TYPE_Q8_0: return 32;
    case GGML_TYPE_Q4_K: return 256;
    case GGML_TYPE_Q5_K: return 256;
    case GGML_TYPE_Q6_K: return 256;
    case GGML_TYPE_F16:  return 1;
    case GGML_TYPE_BF16: return 1;
    case GGML_TYPE_F32:  return 1;
    default:             return 0;
    }
}

size_t dequant_row_bytes(int n_values, ggml_type_t type)
{
    int bs = dequant_type_block_size(type);
    if (bs == 0) return 0;
    size_t bb = dequant_type_block_bytes(type);
    return ((size_t)n_values / (size_t)bs) * bb;
}

const char *dequant_type_name(ggml_type_t type)
{
    switch (type) {
    case GGML_TYPE_Q4_0: return "Q4_0";
    case GGML_TYPE_Q8_0: return "Q8_0";
    case GGML_TYPE_Q4_K: return "Q4_K";
    case GGML_TYPE_Q5_K: return "Q5_K";
    case GGML_TYPE_Q6_K: return "Q6_K";
    case GGML_TYPE_F16:  return "F16";
    case GGML_TYPE_BF16: return "BF16";
    case GGML_TYPE_F32:  return "F32";
    default:             return "UNKNOWN";
    }
}

/* ---- Main dequantization entry point ---- */

int dequant_row(const void *src, float *dst, int n_values, ggml_type_t type)
{
    if (n_values <= 0) return -2;

    int bs = dequant_type_block_size(type);
    if (bs == 0) return -1;  /* unsupported type */

    /* Quantized types require n_values to be a multiple of block_size */
    if (bs > 1 && (n_values % bs != 0)) return -2;

    int n_blocks = n_values / bs;

    switch (type) {
    case GGML_TYPE_Q4_0:
        dequant_q4_0(src, dst, n_blocks);
        return 0;
    case GGML_TYPE_Q8_0:
        dequant_q8_0(src, dst, n_blocks);
        return 0;
    case GGML_TYPE_Q4_K:
        dequant_q4_k(src, dst, n_blocks);
        return 0;
    case GGML_TYPE_Q5_K:
        dequant_q5_k(src, dst, n_blocks);
        return 0;
    case GGML_TYPE_Q6_K:
        dequant_q6_k(src, dst, n_blocks);
        return 0;
    case GGML_TYPE_F16:
        dequant_f16(src, dst, n_values);
        return 0;
    case GGML_TYPE_BF16:
        dequant_bf16(src, dst, n_values);
        return 0;
    case GGML_TYPE_F32:
        dequant_f32(src, dst, n_values);
        return 0;
    default:
        return -1;
    }
}
