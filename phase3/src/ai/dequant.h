/*
 * JARVIS AI-OS Phase 3 — GGUF Dequantization Routines
 *
 * Converts quantized GGUF tensor data (Q4_0, Q8_0, F16) to float32.
 * Pure C11, no intrinsics, no external dependencies beyond gguf_parser.h.
 */

#ifndef DEQUANT_H
#define DEQUANT_H

#include "gguf_parser.h"  /* for ggml_type_t */
#include <stddef.h>

/* Block structs — packed, no padding */
typedef struct __attribute__((packed)) {
    uint16_t d;       /* scale as float16 */
    uint8_t  qs[16];  /* 32 nibbles, 2 per byte */
} q4_0_block_t;
_Static_assert(sizeof(q4_0_block_t) == 18, "Q4_0 block must be 18 bytes");

typedef struct __attribute__((packed)) {
    uint16_t d;       /* scale as float16 */
    int8_t   qs[32];  /* 32 quantized values */
} q8_0_block_t;
_Static_assert(sizeof(q8_0_block_t) == 34, "Q8_0 block must be 34 bytes");

/* F16 to F32 conversion (bit manipulation, no intrinsics) */
float f16_to_f32(uint16_t h);

/* Dequantize n_values from src to dst. Returns 0 success, -1 unsupported, -2 bad count */
int dequant_row(const void *src, float *dst, int n_values, ggml_type_t type);

size_t dequant_type_block_bytes(ggml_type_t type);
int    dequant_type_block_size(ggml_type_t type);
size_t dequant_row_bytes(int n_values, ggml_type_t type);
const char *dequant_type_name(ggml_type_t type);

#endif /* DEQUANT_H */
