/*
 * JARVIS AI-OS Phase 3 -- TurboQuant KV Cache Compression
 *
 * Implements the TurboQuant algorithm (arXiv 2504.19874, ICLR 2026):
 *   Stage 1: Random rotation + Lloyd-Max scalar quantization (MSE-optimal)
 *   Stage 2: 1-bit QJL residual correction (unbiased inner products for keys)
 *
 * Keys use (b-1)-bit MSE + 1-bit QJL = b total bits per coordinate.
 * Values use b-bit MSE only (softmax averaging mitigates per-vector MSE errors).
 *
 * For Llama 3.2 1B (head_dim=64, 3-bit):
 *   F32 KV cache:   32 MB
 *   TQ compressed:  ~3.9 MB  (8.2x compression)
 *
 * Pure C11, no C++ dependencies.
 */

#ifndef TURBOQUANT_H
#define TURBOQUANT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define TQ_MAX_HEAD_DIM   128
#define TQ_MAX_BITS       4
#define TQ_MAX_CENTROIDS  (1 << TQ_MAX_BITS)  /* 16 */

/* Paper configs for d=128 (arXiv 2504.19874 Table 1) */
/* 3.5-bit effective: 32 outlier at 4b, 96 regular at 3b */
#define TQ_CONFIG_35BIT(state, seed) \
    tq_init_mixed((state), 128, 4, 3, 4, 3, 32, (seed))

/* 2.5-bit effective: 32 outlier at 3b, 96 regular at 2b */
#define TQ_CONFIG_25BIT(state, seed) \
    tq_init_mixed((state), 128, 3, 2, 3, 2, 32, (seed))

/* ---- Lloyd-Max codebook ---- */

typedef struct {
    float centroids[TQ_MAX_CENTROIDS];      /* Optimal quantization levels */
    float boundaries[TQ_MAX_CENTROIDS + 1]; /* Decision boundaries (n+1: edges + midpoints) */
    int   n_centroids;                      /* 2^bits */
    int   bits;                             /* Bits per coordinate */
} tq_codebook_t;

/* ---- Pre-computed TurboQuant state (one per layer or shared) ---- */

typedef struct {
    int   d;                    /* head_dim */
    int   key_bits;             /* Uniform key bits (backward compat) */
    int   val_bits;             /* Uniform val bits (backward compat) */
    int   key_bits_high;        /* Bits for outlier key coordinates */
    int   key_bits_low;         /* Bits for regular key coordinates */
    int   val_bits_high;        /* Bits for outlier val coordinates */
    int   val_bits_low;         /* Bits for regular val coordinates */
    int   n_outlier;            /* Number of outlier coordinates (0 = uniform) */
    float *Pi;                  /* d×d orthogonal rotation matrix */
    float *S;                   /* d×d Gaussian QJL projection matrix */
    tq_codebook_t key_cb;       /* Legacy uniform key codebook */
    tq_codebook_t val_cb;       /* Legacy uniform val codebook */
    tq_codebook_t key_cb_high;  /* Outlier key codebook */
    tq_codebook_t key_cb_low;   /* Regular key codebook */
    tq_codebook_t val_cb_high;  /* Outlier val codebook */
    tq_codebook_t val_cb_low;   /* Regular val codebook */
    uint64_t seed;              /* RNG seed used to generate Pi and S */
} tq_state_t;

/* ---- Compressed key: (b-1)-bit MSE + 1-bit QJL + norms ----
 *
 * Sizes depend on bit-width (d=64 examples):
 *   b=3: 16 (2-bit MSE) + 8 (QJL) + 4 (norms) = 28 bytes vs 256 F32 = 9.1x
 *   b=4: 32 (3-bit MSE) + 8 (QJL) + 4 (norms) = 44 bytes vs 256 F32 = 5.8x
 * Struct is max-sized; use tq_compressed_kv_bytes() for actual sizes.
 */
typedef struct {
    uint8_t  mse_idx[TQ_MAX_HEAD_DIM / 2];    /* Up to 4-bit packed (2 per byte) */
    uint8_t  qjl_signs[TQ_MAX_HEAD_DIM / 8];  /* 1-bit packed, 8 per byte */
    uint16_t residual_norm_f16;                /* ||x - x_mse||_2 */
    uint16_t vec_norm_f16;                     /* ||x||_2 */
} tq_ckey_t;

/* ---- Compressed value: b-bit MSE + norm ----
 *
 * For d=64, b=3: 32 + 2 = 34 bytes (vs 256 F32 = 7.5x)
 * For d=128, b=3: 64 + 2 = 66 bytes (vs 512 F32 = 7.8x)
 */
typedef struct {
    uint8_t  mse_idx[TQ_MAX_HEAD_DIM / 2];    /* 4-bit packed, 2 per byte */
    uint16_t vec_norm_f16;                     /* ||x||_2 */
} tq_cval_t;

/* ---- API ---- */

/**
 * Initialize TurboQuant state.
 * Generates rotation matrix Pi and QJL matrix S from seed.
 * Computes Lloyd-Max codebooks for the given bit widths.
 *
 * @param state    Output state struct
 * @param head_dim Head dimension (must be <= TQ_MAX_HEAD_DIM and even)
 * @param key_bits Total bits per key coordinate (2-4, keys use key_bits-1 for MSE + 1 for QJL)
 * @param val_bits Bits per value coordinate (1-4, values use all bits for MSE)
 * @param seed     RNG seed for rotation/projection matrix generation
 * @return 0 on success, -1 on error
 */
int tq_init(tq_state_t *state, int head_dim, int key_bits, int val_bits,
            uint64_t seed);

/**
 * Initialize TurboQuant state with mixed bit-width allocation.
 * Supports per-coordinate bit allocation: n_outlier coordinates use
 * key_bits_high/val_bits_high, remaining (head_dim - n_outlier) use
 * key_bits_low/val_bits_low. Set n_outlier=0 for uniform quantization.
 *
 * @param state          Output state struct
 * @param head_dim       Head dimension (must be <= TQ_MAX_HEAD_DIM and divisible by 8)
 * @param key_bits_high  Total bits for outlier key coordinates (2-4)
 * @param key_bits_low   Total bits for regular key coordinates (2-4)
 * @param val_bits_high  Total bits for outlier value coordinates (1-4)
 * @param val_bits_low   Total bits for regular value coordinates (1-4)
 * @param n_outlier      Number of outlier coordinates (0 = uniform)
 * @param seed           RNG seed for rotation/projection matrix generation
 * @return 0 on success, -1 on error
 */
int tq_init_mixed(tq_state_t *state, int head_dim,
                  int key_bits_high, int key_bits_low,
                  int val_bits_high, int val_bits_low,
                  int n_outlier, uint64_t seed);

/**
 * Compress a key vector (one attention head, d floats).
 * Applies rotation, Lloyd-Max quantization, and QJL residual correction.
 */
void tq_compress_key(const tq_state_t *state, const float *key,
                     tq_ckey_t *out);

/**
 * Compress a value vector (one attention head, d floats).
 * Applies rotation and Lloyd-Max quantization (no QJL).
 */
void tq_compress_val(const tq_state_t *state, const float *val,
                     tq_cval_t *out);

/**
 * Decompress a key vector (MSE reconstruction only, no QJL correction).
 * Output is the MSE-optimal reconstruction, not the QJL-corrected version.
 */
void tq_decompress_key(const tq_state_t *state, const tq_ckey_t *in,
                       float *key);

/**
 * Decompress a value vector.
 */
void tq_decompress_val(const tq_state_t *state, const tq_cval_t *in,
                       float *val);

/**
 * Compute inner product <query, compressed_key> using QJL correction.
 * Returns unbiased estimate: <query, key_mse> + gamma * sqrt(pi/2)/d * <S*query, signs>
 *
 * This is the key operation for attention score computation.
 */
float tq_dot_key(const tq_state_t *state, const float *query,
                 const tq_ckey_t *ckey);

/**
 * Compressed size per position per layer (bytes), for n_kv_heads heads.
 */
size_t tq_compressed_kv_bytes(const tq_state_t *state, int n_kv_heads);

/**
 * F32 baseline size per position per layer (bytes), for n_kv_heads heads.
 */
size_t tq_baseline_kv_bytes(int head_dim, int n_kv_heads);

/**
 * TQ state overhead (bytes) — rotation + projection matrices.
 */
size_t tq_state_bytes(const tq_state_t *state);

/**
 * Free TQ state (Pi and S matrices).
 */
void tq_free(tq_state_t *state);

/* General bitstream pack/unpack for mixed bit-width configs */
void tq_pack_bits(uint8_t *buf, int idx, int val, int bits);
int  tq_unpack_bits(const uint8_t *buf, int idx, int bits);

#ifdef __cplusplus
}
#endif

#endif /* TURBOQUANT_H */
