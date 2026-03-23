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
    case GGML_TYPE_F16:  return 2;
    case GGML_TYPE_F32:  return 4;
    default:             return 0;
    }
}

int dequant_type_block_size(ggml_type_t type)
{
    switch (type) {
    case GGML_TYPE_Q4_0: return 32;
    case GGML_TYPE_Q8_0: return 32;
    case GGML_TYPE_F16:  return 1;
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
    case GGML_TYPE_F16:  return "F16";
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
    case GGML_TYPE_F16:
        dequant_f16(src, dst, n_values);
        return 0;
    case GGML_TYPE_F32:
        dequant_f32(src, dst, n_values);
        return 0;
    default:
        return -1;
    }
}
