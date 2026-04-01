/*
 * JARVIS AI-OS Phase 3 -- TurboQuant KV Cache Compression
 *
 * Algorithm (arXiv 2504.19874):
 *   1. Generate random orthogonal matrix Pi via QR of Gaussian (Haar measure)
 *   2. Rotate vector: y = Pi * x
 *   3. After rotation, coordinates follow Beta((d-1)/2, (d-1)/2) on [-1,1]
 *   4. Apply Lloyd-Max scalar quantization per coordinate
 *   5. For keys: add 1-bit QJL residual correction for unbiased inner products
 *
 * Pure C11, no C++ dependencies.
 */

#include "turboquant.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * RNG (XORshift64 + Box-Muller for Gaussian)
 * ============================================================ */

static uint64_t tq_rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static float tq_rng_uniform(uint64_t *s)
{
    return (float)(tq_rng_next(s) >> 11) / (float)(1ULL << 53);
}

/* Box-Muller: generate one N(0,1) sample (discards the second) */
static float tq_rng_gaussian(uint64_t *s)
{
    float u1, u2;
    do { u1 = tq_rng_uniform(s); } while (u1 < 1e-30f);
    u2 = tq_rng_uniform(s);
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265358979f * u2);
}

/* ============================================================
 * F16 conversion (matches dequant.h convention)
 * ============================================================ */

static uint16_t f32_to_f16(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    uint32_t s = (v.u >> 16) & 0x8000;
    int e = ((v.u >> 23) & 0xFF) - 127 + 15;
    uint32_t m = v.u & 0x007FFFFF;
    if (e <= 0) return (uint16_t)s;              /* Underflow to zero */
    if (e >= 31) return (uint16_t)(s | 0x7C00);  /* Overflow to inf */
    return (uint16_t)(s | ((uint32_t)e << 10) | (m >> 13));
}

static float f16_to_f32(uint16_t h)
{
    uint32_t s = (uint32_t)(h & 0x8000) << 16;
    uint32_t e = (h >> 10) & 0x1F;
    uint32_t m = h & 0x03FF;
    if (e == 0) { if (m == 0) { union { uint32_t u; float f; } r; r.u = s; return r.f; }
                  e = 1; while (!(m & 0x0400)) { m <<= 1; e--; } m &= 0x03FF; }
    else if (e == 31) { union { uint32_t u; float f; } r; r.u = s | 0x7F800000 | (m << 13); return r.f; }
    union { uint32_t u; float f; } r;
    r.u = s | ((e + 127 - 15) << 23) | (m << 13);
    return r.f;
}

/* ============================================================
 * Lloyd-Max codebooks — Beta-optimal and Gaussian fallback
 *
 * After rotating a unit vector by a Haar-distributed orthogonal matrix,
 * each coordinate follows Beta((d-1)/2, (d-1)/2) on [-1, 1].
 * These are the exact Lloyd-Max centroids for that distribution.
 * Source: 0xSero/turboquant (exact numerical integration).
 *
 * Gaussian N(0, 1/d) approximation is kept as fallback for unsupported d.
 * For d=64 the difference is ~2%; for d=128 it's ~1%.
 * ============================================================ */

/* ---- Beta-optimal centroids for d=64: Beta(31.5, 31.5) ---- */
static const float BETA_D64_1BIT[] = { -0.10012591f,  0.10012591f };
static const float BETA_D64_2BIT[] = { -0.18749585f, -0.05651437f,  0.05651437f,  0.18749585f };
static const float BETA_D64_3BIT[] = {
    -0.26390745f, -0.16616104f, -0.09382718f, -0.03046731f,
     0.03046731f,  0.09382718f,  0.16616104f,  0.26390745f
};
static const float BETA_D64_4BIT[] = {
    -0.33074890f, -0.25285796f, -0.19879805f, -0.15487006f,
    -0.11643835f, -0.08127422f, -0.04806602f, -0.01591089f,
     0.01591089f,  0.04806602f,  0.08127422f,  0.11643835f,
     0.15487006f,  0.19879805f,  0.25285796f,  0.33074890f
};

/* ---- Beta-optimal centroids for d=128: Beta(63.5, 63.5) ---- */
static const float BETA_D128_1BIT[] = { -0.07066157f,  0.07066157f };
static const float BETA_D128_2BIT[] = { -0.13304020f, -0.03999095f,  0.03999095f,  0.13304020f };
static const float BETA_D128_3BIT[] = {
    -0.18839061f, -0.11813298f, -0.06658060f, -0.02160247f,
     0.02160247f,  0.06658060f,  0.11813298f,  0.18839061f
};
static const float BETA_D128_4BIT[] = {
    -0.23762719f, -0.18079373f, -0.14176165f, -0.11024707f,
    -0.08279257f, -0.05774454f, -0.03413403f, -0.01129650f,
     0.01129650f,  0.03413403f,  0.05774454f,  0.08279257f,
     0.11024707f,  0.14176165f,  0.18079373f,  0.23762719f
};

/* ---- Gaussian N(0,1) fallback (for d != 64 and d != 128) ---- */
static const float LM_1BIT[] = { -0.7979f, 0.7979f };
static const float LM_2BIT[] = { -1.5104f, -0.4528f, 0.4528f, 1.5104f };
static const float LM_3BIT[] = { -2.1520f, -1.3440f, -0.7560f, -0.2451f,
                                   0.2451f,  0.7560f,  1.3440f,  2.1520f };
static const float LM_4BIT[] = {
    -2.7326f, -2.0690f, -1.6180f, -1.2562f,
    -0.9424f, -0.6568f, -0.3881f, -0.1284f,
     0.1284f,  0.3881f,  0.6568f,  0.9424f,
     1.2562f,  1.6180f,  2.0690f,  2.7326f
};

/* Return Beta-optimal centroids for known d, or NULL for Gaussian fallback */
static const float *beta_centroids(int bits, int head_dim)
{
    if (head_dim == 64) {
        switch (bits) {
            case 1: return BETA_D64_1BIT;
            case 2: return BETA_D64_2BIT;
            case 3: return BETA_D64_3BIT;
            case 4: return BETA_D64_4BIT;
        }
    } else if (head_dim == 128) {
        switch (bits) {
            case 1: return BETA_D128_1BIT;
            case 2: return BETA_D128_2BIT;
            case 3: return BETA_D128_3BIT;
            case 4: return BETA_D128_4BIT;
        }
    }
    return NULL;
}

static void init_codebook(tq_codebook_t *cb, int bits, int head_dim)
{
    int n;
    switch (bits) {
        case 1: n = 2;  break;
        case 2: n = 4;  break;
        case 3: n = 8;  break;
        case 4: n = 16; break;
        default: n = 4; bits = 2; break;
    }
    cb->bits = bits;
    cb->n_centroids = n;

    /* Try Beta-optimal centroids first */
    const float *src = beta_centroids(bits, head_dim);
    if (src) {
        /* Pre-computed centroids already in correct range for unit vectors */
        for (int i = 0; i < n; i++)
            cb->centroids[i] = src[i];
    } else {
        /* Fallback: N(0,1) centroids scaled by 1/sqrt(d) */
        float sigma = 1.0f / sqrtf((float)head_dim);
        const float *gauss;
        switch (bits) {
            case 1: gauss = LM_1BIT; break;
            case 2: gauss = LM_2BIT; break;
            case 3: gauss = LM_3BIT; break;
            case 4: gauss = LM_4BIT; break;
            default: gauss = LM_2BIT; break;
        }
        for (int i = 0; i < n; i++)
            cb->centroids[i] = gauss[i] * sigma;
    }

    /* Decision boundaries: interior midpoints + sentinel (matches quantize_scalar) */
    for (int i = 0; i < n - 1; i++)
        cb->boundaries[i] = (cb->centroids[i] + cb->centroids[i + 1]) * 0.5f;
    cb->boundaries[n - 1] = 1e30f;
}

/* Find nearest centroid index for a scalar value */
static int quantize_scalar(const tq_codebook_t *cb, float val)
{
    /* Linear search (n_centroids <= 16, branch-free for small n) */
    for (int i = 0; i < cb->n_centroids - 1; i++) {
        if (val < cb->boundaries[i])
            return i;
    }
    return cb->n_centroids - 1;
}

/* ============================================================
 * Random orthogonal matrix via Modified Gram-Schmidt
 * ============================================================ */

static float vec_dot(const float *a, const float *b, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

static float vec_norm(const float *a, int n)
{
    return sqrtf(vec_dot(a, a, n));
}

static void gen_orthogonal_matrix(float *Q, int d, uint64_t *rng)
{
    /* Fill with Gaussian random values */
    for (int i = 0; i < d * d; i++)
        Q[i] = tq_rng_gaussian(rng);

    /* Modified Gram-Schmidt orthogonalization */
    for (int j = 0; j < d; j++) {
        float *col_j = Q + j * d;  /* Column j stored as contiguous d floats */

        /* Subtract projections onto previous columns */
        for (int i = 0; i < j; i++) {
            float *col_i = Q + i * d;
            float proj = vec_dot(col_j, col_i, d);
            for (int k = 0; k < d; k++)
                col_j[k] -= proj * col_i[k];
        }

        /* Normalize */
        float nrm = vec_norm(col_j, d);
        if (nrm > 1e-10f) {
            float inv = 1.0f / nrm;
            for (int k = 0; k < d; k++)
                col_j[k] *= inv;
        }
    }
}

static void gen_gaussian_matrix(float *M, int d, uint64_t *rng)
{
    for (int i = 0; i < d * d; i++)
        M[i] = tq_rng_gaussian(rng);
}

/* ============================================================
 * Matrix-vector multiply: out = M * x  (M is d x d, column-major)
 * ============================================================ */

static void matvec(const float *M, const float *x, float *out, int d)
{
    /* M stored column-major: M[col * d + row] */
    memset(out, 0, (size_t)d * sizeof(float));
    for (int col = 0; col < d; col++) {
        float xc = x[col];
        const float *mcol = M + col * d;
        for (int row = 0; row < d; row++)
            out[row] += mcol[row] * xc;
    }
}

/* Transpose multiply: out = M^T * x */
static void matvec_t(const float *M, const float *x, float *out, int d)
{
    for (int col = 0; col < d; col++) {
        const float *mcol = M + col * d;
        out[col] = vec_dot(mcol, x, d);
    }
}

/* ============================================================
 * Bit packing helpers
 * ============================================================ */

/* Pack 2-bit index at position i */
static void __attribute__((unused)) pack_2bit(uint8_t *buf, int i, int val)
{
    int byte = i >> 2;
    int shift = (i & 3) * 2;
    buf[byte] = (uint8_t)((buf[byte] & ~(3 << shift)) | ((val & 3) << shift));
}

/* Unpack 2-bit index at position i */
static int __attribute__((unused)) unpack_2bit(const uint8_t *buf, int i)
{
    return (buf[i >> 2] >> ((i & 3) * 2)) & 3;
}

/* Pack 4-bit (nibble) index at position i */
static void __attribute__((unused)) pack_4bit(uint8_t *buf, int i, int val)
{
    int byte = i >> 1;
    if (i & 1)
        buf[byte] = (uint8_t)((buf[byte] & 0x0F) | ((val & 0xF) << 4));
    else
        buf[byte] = (uint8_t)((buf[byte] & 0xF0) | (val & 0xF));
}

/* Unpack 4-bit index at position i */
static int __attribute__((unused)) unpack_4bit(const uint8_t *buf, int i)
{
    if (i & 1)
        return (buf[i >> 1] >> 4) & 0xF;
    else
        return buf[i >> 1] & 0xF;
}

/* Generic index pack/unpack based on bit width (kept for reference) */
static void __attribute__((unused)) pack_idx(uint8_t *buf, int i, int val, int bits)
{
    switch (bits) {
        case 1: {
            int byte = i >> 3;
            int bit  = i & 7;
            if (val)
                buf[byte] |= (uint8_t)(1 << bit);
            else
                buf[byte] &= (uint8_t)~(1 << bit);
            break;
        }
        case 2: pack_2bit(buf, i, val); break;
        default: pack_4bit(buf, i, val); break;
    }
}

static int __attribute__((unused)) unpack_idx(const uint8_t *buf, int i, int bits)
{
    switch (bits) {
        case 1: return (buf[i >> 3] >> (i & 7)) & 1;
        case 2: return unpack_2bit(buf, i);
        default: return unpack_4bit(buf, i);
    }
}

/* Pack 1-bit sign at position i */
static void pack_1bit(uint8_t *buf, int i, int val)
{
    int byte = i >> 3;
    int bit  = i & 7;
    if (val)
        buf[byte] |= (uint8_t)(1 << bit);
    else
        buf[byte] &= (uint8_t)~(1 << bit);
}

/* Unpack 1-bit sign at position i (returns 0 or 1) */
static int unpack_1bit(const uint8_t *buf, int i)
{
    return (buf[i >> 3] >> (i & 7)) & 1;
}

/* General bitstream packer: pack 'bits' bits of 'val' at position 'idx' */
void tq_pack_bits(uint8_t *buf, int idx, int val, int bits) {
    int bit_pos = idx * bits;
    for (int b = 0; b < bits; b++) {
        int byte_idx = (bit_pos + b) / 8;
        int bit_idx  = (bit_pos + b) % 8;
        if (val & (1 << b))
            buf[byte_idx] |= (uint8_t)(1 << bit_idx);
        else
            buf[byte_idx] &= (uint8_t)~(1 << bit_idx);
    }
}

/* General bitstream unpacker */
int tq_unpack_bits(const uint8_t *buf, int idx, int bits) {
    int bit_pos = idx * bits;
    int val = 0;
    for (int b = 0; b < bits; b++) {
        int byte_idx = (bit_pos + b) / 8;
        int bit_idx  = (bit_pos + b) % 8;
        if (buf[byte_idx] & (1 << bit_idx))
            val |= (1 << b);
    }
    return val;
}

/* ============================================================
 * Public API
 * ============================================================ */

int tq_init_mixed(tq_state_t *state, int head_dim,
                  int key_bits_high, int key_bits_low,
                  int val_bits_high, int val_bits_low,
                  int n_outlier, uint64_t seed)
{
    if (!state || head_dim <= 0 || head_dim > TQ_MAX_HEAD_DIM) return -1;
    if (head_dim % 8 != 0) return -1;
    if (key_bits_high < 2 || key_bits_high > TQ_MAX_BITS) return -1;
    if (key_bits_low  < 2 || key_bits_low  > TQ_MAX_BITS) return -1;
    if (val_bits_high < 1 || val_bits_high > TQ_MAX_BITS) return -1;
    if (val_bits_low  < 1 || val_bits_low  > TQ_MAX_BITS) return -1;
    if (n_outlier < 0 || n_outlier > head_dim) return -1;

    memset(state, 0, sizeof(*state));
    state->d             = head_dim;
    state->key_bits      = key_bits_high;  /* backward compat: legacy field mirrors high */
    state->val_bits      = val_bits_high;  /* backward compat */
    state->key_bits_high = key_bits_high;
    state->key_bits_low  = key_bits_low;
    state->val_bits_high = val_bits_high;
    state->val_bits_low  = val_bits_low;
    state->n_outlier     = n_outlier;
    state->seed          = seed;

    /* Allocate rotation and QJL matrices */
    size_t mat_size = (size_t)head_dim * (size_t)head_dim * sizeof(float);
    state->Pi = (float *)malloc(mat_size);
    state->S  = (float *)malloc(mat_size);
    if (!state->Pi || !state->S) {
        tq_free(state);
        return -1;
    }

    uint64_t rng = seed;
    gen_orthogonal_matrix(state->Pi, head_dim, &rng);
    gen_gaussian_matrix(state->S,  head_dim, &rng);

    /* Initialize all six codebooks.
     * Keys use (bits-1) for MSE (1 bit reserved for QJL residual).
     * Values use full bits for MSE (no QJL).
     * Legacy key_cb / val_cb mirror the high codebooks for backward compat. */
    init_codebook(&state->key_cb_high, key_bits_high - 1, head_dim);
    init_codebook(&state->key_cb_low,  key_bits_low  - 1, head_dim);
    init_codebook(&state->val_cb_high, val_bits_high,     head_dim);
    init_codebook(&state->val_cb_low,  val_bits_low,      head_dim);

    /* Legacy uniform codebooks point to the same parameters as high codebooks */
    init_codebook(&state->key_cb, key_bits_high - 1, head_dim);
    init_codebook(&state->val_cb, val_bits_high,     head_dim);

    return 0;
}

int tq_init(tq_state_t *state, int head_dim, int key_bits, int val_bits,
            uint64_t seed)
{
    return tq_init_mixed(state, head_dim,
                         key_bits, key_bits,
                         val_bits, val_bits,
                         0, seed);
}

void tq_free(tq_state_t *state)
{
    if (!state) return;
    free(state->Pi);
    free(state->S);
    memset(state, 0, sizeof(*state));
}

void tq_compress_key(const tq_state_t *state, const float *key,
                     tq_ckey_t *out)
{
    int d = state->d;
    float rotated[TQ_MAX_HEAD_DIM];
    float mse_recon[TQ_MAX_HEAD_DIM];

    /* Compute and store vector norm */
    float vnorm = vec_norm(key, d);
    out->vec_norm_f16 = f32_to_f16(vnorm);

    /* Normalize to unit vector for quantization */
    float unit[TQ_MAX_HEAD_DIM];
    if (vnorm > 1e-10f) {
        float inv = 1.0f / vnorm;
        for (int i = 0; i < d; i++) unit[i] = key[i] * inv;
    } else {
        memset(unit, 0, (size_t)d * sizeof(float));
    }

    /* Stage 1: Rotate */
    matvec(state->Pi, unit, rotated, d);

    /* Stage 1: Lloyd-Max quantize each coordinate (mixed bit-width when n_outlier > 0) */
    memset(out->mse_idx, 0, sizeof(out->mse_idx));
    int bit_offset = 0;
    for (int i = 0; i < d; i++) {
        const tq_codebook_t *cb;
        int mse_bits;
        if (state->n_outlier > 0 && i < state->n_outlier) {
            cb = &state->key_cb_high;
            mse_bits = state->key_bits_high - 1;
        } else if (state->n_outlier > 0) {
            cb = &state->key_cb_low;
            mse_bits = state->key_bits_low - 1;
        } else {
            cb = &state->key_cb;
            mse_bits = state->key_cb.bits;
        }
        int idx = quantize_scalar(cb, rotated[i]);
        /* Pack at explicit bit offset */
        for (int b = 0; b < mse_bits; b++) {
            int byte_idx = (bit_offset + b) / 8;
            int bit_idx  = (bit_offset + b) % 8;
            if (idx & (1 << b))
                out->mse_idx[byte_idx] |= (uint8_t)(1 << bit_idx);
        }
        rotated[i] = cb->centroids[idx];  /* Quantized value */
        bit_offset += mse_bits;
    }

    /* Inverse rotate to get MSE reconstruction (for residual) */
    matvec_t(state->Pi, rotated, mse_recon, d);

    /* Compute residual r = unit - mse_recon */
    float residual[TQ_MAX_HEAD_DIM];
    memset(residual, 0, sizeof(residual));
    for (int i = 0; i < d; i++)
        residual[i] = unit[i] - mse_recon[i];

    float rnorm = vec_norm(residual, d);
    out->residual_norm_f16 = f32_to_f16(rnorm);

    /* Stage 2: QJL — sign(S * residual) */
    float s_r[TQ_MAX_HEAD_DIM];
    matvec(state->S, residual, s_r, d);

    memset(out->qjl_signs, 0, sizeof(out->qjl_signs));
    for (int i = 0; i < d; i++)
        pack_1bit(out->qjl_signs, i, s_r[i] >= 0.0f ? 1 : 0);
}

void tq_compress_val(const tq_state_t *state, const float *val,
                     tq_cval_t *out)
{
    int d = state->d;
    float rotated[TQ_MAX_HEAD_DIM];

    /* Store vector norm */
    float vnorm = vec_norm(val, d);
    out->vec_norm_f16 = f32_to_f16(vnorm);

    /* Normalize */
    float unit[TQ_MAX_HEAD_DIM];
    if (vnorm > 1e-10f) {
        float inv = 1.0f / vnorm;
        for (int i = 0; i < d; i++) unit[i] = val[i] * inv;
    } else {
        memset(unit, 0, (size_t)d * sizeof(float));
    }

    /* Rotate */
    matvec(state->Pi, unit, rotated, d);

    /* Lloyd-Max quantize (mixed bit-width when n_outlier > 0) */
    memset(out->mse_idx, 0, sizeof(out->mse_idx));
    int bit_offset = 0;
    for (int i = 0; i < d; i++) {
        const tq_codebook_t *cb;
        int mse_bits;
        if (state->n_outlier > 0 && i < state->n_outlier) {
            cb = &state->val_cb_high;
            mse_bits = state->val_bits_high;
        } else if (state->n_outlier > 0) {
            cb = &state->val_cb_low;
            mse_bits = state->val_bits_low;
        } else {
            cb = &state->val_cb;
            mse_bits = state->val_cb.bits;
        }
        int idx = quantize_scalar(cb, rotated[i]);
        /* Pack at explicit bit offset */
        for (int b = 0; b < mse_bits; b++) {
            int byte_idx = (bit_offset + b) / 8;
            int bit_idx  = (bit_offset + b) % 8;
            if (idx & (1 << b))
                out->mse_idx[byte_idx] |= (uint8_t)(1 << bit_idx);
        }
        bit_offset += mse_bits;
    }
}

void tq_decompress_key(const tq_state_t *state, const tq_ckey_t *in,
                       float *key)
{
    int d = state->d;
    float quantized[TQ_MAX_HEAD_DIM];

    /* Reconstruct quantized rotated vector (mixed bit-width when n_outlier > 0) */
    int bit_offset = 0;
    for (int i = 0; i < d; i++) {
        const tq_codebook_t *cb;
        int mse_bits;
        if (state->n_outlier > 0 && i < state->n_outlier) {
            cb = &state->key_cb_high;
            mse_bits = state->key_bits_high - 1;
        } else if (state->n_outlier > 0) {
            cb = &state->key_cb_low;
            mse_bits = state->key_bits_low - 1;
        } else {
            cb = &state->key_cb;
            mse_bits = state->key_cb.bits;
        }
        int idx = 0;
        for (int b = 0; b < mse_bits; b++) {
            int byte_idx = (bit_offset + b) / 8;
            int bit_idx  = (bit_offset + b) % 8;
            if (in->mse_idx[byte_idx] & (1 << bit_idx))
                idx |= (1 << b);
        }
        if (idx >= cb->n_centroids) idx = cb->n_centroids - 1;
        quantized[i] = cb->centroids[idx];
        bit_offset += mse_bits;
    }

    /* Inverse rotate */
    float unit_recon[TQ_MAX_HEAD_DIM];
    matvec_t(state->Pi, quantized, unit_recon, d);

    /* Re-scale by stored norm */
    float vnorm = f16_to_f32(in->vec_norm_f16);
    for (int i = 0; i < d; i++)
        key[i] = unit_recon[i] * vnorm;
}

void tq_decompress_val(const tq_state_t *state, const tq_cval_t *in,
                       float *val)
{
    int d = state->d;
    float quantized[TQ_MAX_HEAD_DIM];

    /* Reconstruct quantized rotated vector (mixed bit-width when n_outlier > 0) */
    int bit_offset = 0;
    for (int i = 0; i < d; i++) {
        const tq_codebook_t *cb;
        int mse_bits;
        if (state->n_outlier > 0 && i < state->n_outlier) {
            cb = &state->val_cb_high;
            mse_bits = state->val_bits_high;
        } else if (state->n_outlier > 0) {
            cb = &state->val_cb_low;
            mse_bits = state->val_bits_low;
        } else {
            cb = &state->val_cb;
            mse_bits = state->val_cb.bits;
        }
        int idx = 0;
        for (int b = 0; b < mse_bits; b++) {
            int byte_idx = (bit_offset + b) / 8;
            int bit_idx  = (bit_offset + b) % 8;
            if (in->mse_idx[byte_idx] & (1 << bit_idx))
                idx |= (1 << b);
        }
        if (idx >= cb->n_centroids) idx = cb->n_centroids - 1;
        quantized[i] = cb->centroids[idx];
        bit_offset += mse_bits;
    }

    /* Inverse rotate */
    float unit_recon[TQ_MAX_HEAD_DIM];
    matvec_t(state->Pi, quantized, unit_recon, d);

    /* Re-scale by stored norm */
    float vnorm = f16_to_f32(in->vec_norm_f16);
    for (int i = 0; i < d; i++)
        val[i] = unit_recon[i] * vnorm;
}

float tq_dot_key(const tq_state_t *state, const float *query,
                 const tq_ckey_t *ckey)
{
    int d = state->d;

    /* Term 1: <query, key_mse_reconstruction> */
    float key_mse[TQ_MAX_HEAD_DIM];
    tq_decompress_key(state, ckey, key_mse);
    float term1 = vec_dot(query, key_mse, d);

    /* Term 2: QJL correction
     * gamma * sqrt(pi/2) / d * <S*query, signs>
     * where signs are {-1, +1} from the stored QJL bits */
    float gamma = f16_to_f32(ckey->residual_norm_f16);
    float vnorm = f16_to_f32(ckey->vec_norm_f16);

    if (gamma < 1e-10f || vnorm < 1e-10f)
        return term1;

    /* Compute S * query */
    float s_q[TQ_MAX_HEAD_DIM];
    matvec(state->S, query, s_q, d);

    /* <S*query, signs> where signs are +1/-1 */
    float dot_signs = 0.0f;
    for (int i = 0; i < d; i++) {
        float sign_val = unpack_1bit(ckey->qjl_signs, i) ? 1.0f : -1.0f;
        dot_signs += s_q[i] * sign_val;
    }

    /* QJL correction: gamma * vnorm * sqrt(pi/2) / d * dot_signs */
    float sqrt_pi_over_2 = 1.2533141f;  /* sqrt(pi/2) */
    float term2 = gamma * vnorm * sqrt_pi_over_2 / (float)d * dot_signs;

    return term1 + term2;
}

size_t tq_compressed_kv_bytes(const tq_state_t *state, int n_kv_heads)
{
    int d = state->d;
    int key_mse_bits = state->key_cb.bits;   /* b-1 */
    int val_mse_bits = state->val_cb.bits;   /* b */

    /* Key MSE indices: ceil(d * key_mse_bits / 8) */
    size_t key_mse_bytes = ((size_t)d * (size_t)key_mse_bits + 7) / 8;
    /* Key: MSE + d/8 (QJL signs) + 2 (rnorm) + 2 (vnorm) */
    size_t key_bytes = key_mse_bytes + (size_t)(d / 8) + 4;

    /* Value MSE indices: ceil(d * val_mse_bits / 8) */
    size_t val_mse_bytes = ((size_t)d * (size_t)val_mse_bits + 7) / 8;
    /* Value: MSE + 2 (vnorm) */
    size_t val_bytes = val_mse_bytes + 2;

    return (size_t)n_kv_heads * (key_bytes + val_bytes);
}

size_t tq_baseline_kv_bytes(int head_dim, int n_kv_heads)
{
    return (size_t)n_kv_heads * (size_t)head_dim * 2 * sizeof(float);
}

size_t tq_state_bytes(const tq_state_t *state)
{
    size_t mat = (size_t)state->d * (size_t)state->d * sizeof(float);
    return sizeof(tq_state_t) + 2 * mat;  /* Pi + S */
}
