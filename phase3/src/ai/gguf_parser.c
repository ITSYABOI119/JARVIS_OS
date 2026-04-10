/*
 * JARVIS AI-OS Phase 3 — C-Only GGUF File Parser
 *
 * Pure C11 implementation. No C++, no STL, no mmap.
 * Uses fopen/fread for file I/O (stubbed on seL4 to read from AHCI).
 * Supports fmemopen() for parsing from memory buffers.
 *
 * GGUF binary format (little-endian):
 *   [Header]        magic(u32) version(u32) n_tensors(u64) n_kv(u64)
 *   [KV Pairs]      repeated n_kv times: key(string) type(u32) value(varies)
 *   [Tensor Info]   repeated n_tensors: name(string) n_dims(u32) dims(u64[]) type(u32) offset(u64)
 *   [Padding]       aligned to general.alignment (default 32)
 *   [Tensor Data]   raw tensor weight data
 *
 * Strings: uint64_t length + char[length] (NOT null-terminated in file)
 */

#define _POSIX_C_SOURCE 200809L  /* for fmemopen() */

#include "gguf_parser.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ---- Helpers ---- */

static int read_exact(FILE *fp, void *buf, size_t n)
{
    if (fread(buf, 1, n, fp) != n)
        return GGUF_ERR_READ;
    return GGUF_OK;
}

static int read_u8(FILE *fp, uint8_t *out)   { return read_exact(fp, out, 1); }
static int read_i8(FILE *fp, int8_t *out)    { return read_exact(fp, out, 1); }
static int read_u16(FILE *fp, uint16_t *out) { return read_exact(fp, out, 2); }
static int read_i16(FILE *fp, int16_t *out)  { return read_exact(fp, out, 2); }
static int read_u32(FILE *fp, uint32_t *out) { return read_exact(fp, out, 4); }
static int read_i32(FILE *fp, int32_t *out)  { return read_exact(fp, out, 4); }
static int read_u64(FILE *fp, uint64_t *out) { return read_exact(fp, out, 8); }
static int read_i64(FILE *fp, int64_t *out)  { return read_exact(fp, out, 8); }
static int read_f32(FILE *fp, float *out)    { return read_exact(fp, out, 4); }
static int read_f64(FILE *fp, double *out)   { return read_exact(fp, out, 8); }

/* Read a GGUF string: uint64_t length + char[length].
 * Copies into buf (max buf_size-1 chars), null-terminates. */
static int read_gguf_string(FILE *fp, char *buf, size_t buf_size, uint64_t *out_len)
{
    uint64_t len;
    int err = read_u64(fp, &len);
    if (err) return err;

    if (out_len) *out_len = len;

    if (len >= buf_size) {
        /* String too long — read and discard excess */
        size_t to_read = buf_size - 1;
        err = read_exact(fp, buf, to_read);
        if (err) return err;
        buf[to_read] = '\0';

        /* Skip remaining bytes */
        uint64_t remaining = len - to_read;
        while (remaining > 0) {
            char discard[256];
            size_t chunk = remaining > sizeof(discard) ? sizeof(discard) : (size_t)remaining;
            err = read_exact(fp, discard, chunk);
            if (err) return err;
            remaining -= chunk;
        }
        return GGUF_OK;
    }

    err = read_exact(fp, buf, (size_t)len);
    if (err) return err;
    buf[len] = '\0';
    return GGUF_OK;
}

/* SEC-017: Skip bytes — chunk seeks for 32-bit portability (LONG_MAX may be 2^31-1) */
static int skip_bytes(FILE *fp, uint64_t n)
{
    if (n == 0) return GGUF_OK;
    while (n > 0) {
        long chunk = (n > (uint64_t)LONG_MAX) ? LONG_MAX : (long)n;
        if (fseek(fp, chunk, SEEK_CUR) != 0) {
            /* Fallback: read and discard */
            char discard[4096];
            uint64_t remaining = n;
            while (remaining > 0) {
                size_t rd = remaining > sizeof(discard) ? sizeof(discard) : (size_t)remaining;
                if (fread(discard, 1, rd, fp) != rd)
                    return GGUF_ERR_READ;
                remaining -= rd;
            }
            return GGUF_OK;
        }
        n -= (uint64_t)chunk;
    }
    return GGUF_OK;
}

/* Size of a scalar GGUF value type in bytes */
static size_t gguf_value_type_size(uint32_t type)
{
    switch (type) {
        case GGUF_TYPE_UINT8:   return 1;
        case GGUF_TYPE_INT8:    return 1;
        case GGUF_TYPE_UINT16:  return 2;
        case GGUF_TYPE_INT16:   return 2;
        case GGUF_TYPE_UINT32:  return 4;
        case GGUF_TYPE_INT32:   return 4;
        case GGUF_TYPE_FLOAT32: return 4;
        case GGUF_TYPE_BOOL:    return 1;
        case GGUF_TYPE_UINT64:  return 8;
        case GGUF_TYPE_INT64:   return 8;
        case GGUF_TYPE_FLOAT64: return 8;
        default:                return 0;
    }
}

/* Bytes per element for ggml tensor types */
static uint64_t ggml_type_size(uint32_t type)
{
    switch (type) {
        case GGML_TYPE_F32:     return 4;
        case GGML_TYPE_F16:     return 2;
        case GGML_TYPE_Q4_0:    return 18;  /* block of 32 values = 2 + 16 bytes */
        case GGML_TYPE_Q4_1:    return 20;  /* block of 32 values = 4 + 16 bytes */
        case GGML_TYPE_Q5_0:    return 22;  /* block of 32 */
        case GGML_TYPE_Q5_1:    return 24;  /* block of 32 */
        case GGML_TYPE_Q8_0:    return 34;  /* block of 32 values = 2 + 32 bytes */
        case GGML_TYPE_Q8_1:    return 36;  /* block of 32 */
        case GGML_TYPE_Q2_K:    return 84;  /* block of 256 */
        case GGML_TYPE_Q3_K:    return 110; /* block of 256 */
        case GGML_TYPE_Q4_K:    return 144; /* block of 256 */
        case GGML_TYPE_Q5_K:    return 176; /* block of 256 */
        case GGML_TYPE_Q6_K:    return 210; /* block of 256 */
        case GGML_TYPE_Q8_K:    return 292; /* block of 256 */
        case GGML_TYPE_I8:      return 1;
        case GGML_TYPE_I16:     return 2;
        case GGML_TYPE_I32:     return 4;
        case GGML_TYPE_I64:     return 8;
        case GGML_TYPE_F64:     return 8;
        default:                return 0;
    }
}

/* Elements per quantization block */
static uint64_t ggml_type_block_size(uint32_t type)
{
    switch (type) {
        case GGML_TYPE_F32:
        case GGML_TYPE_F16:
        case GGML_TYPE_I8:
        case GGML_TYPE_I16:
        case GGML_TYPE_I32:
        case GGML_TYPE_I64:
        case GGML_TYPE_F64:
            return 1;
        case GGML_TYPE_Q4_0:
        case GGML_TYPE_Q4_1:
        case GGML_TYPE_Q5_0:
        case GGML_TYPE_Q5_1:
        case GGML_TYPE_Q8_0:
        case GGML_TYPE_Q8_1:
            return 32;
        case GGML_TYPE_Q2_K:
        case GGML_TYPE_Q3_K:
        case GGML_TYPE_Q4_K:
        case GGML_TYPE_Q5_K:
        case GGML_TYPE_Q6_K:
        case GGML_TYPE_Q8_K:
            return 256;
        default:
            return 1;
    }
}

/* Calculate total bytes for n_elements of a given type */
static uint64_t ggml_tensor_bytes(uint32_t type, uint64_t n_elements)
{
    uint64_t block_sz = ggml_type_block_size(type);
    uint64_t type_sz  = ggml_type_size(type);
    if (block_sz == 0 || type_sz == 0) return 0;
    uint64_t n_blocks = (n_elements + block_sz - 1) / block_sz;
    return n_blocks * type_sz;
}

/* ---- KV Parsing ---- */

/* SEC-007: Skip a GGUF value with bounded recursion depth */
static int skip_gguf_value(FILE *fp, uint32_t type, int depth)
{
    if (depth > GGUF_MAX_ARRAY_DEPTH) return GGUF_ERR_FORMAT;

    if (type == GGUF_TYPE_STRING) {
        uint64_t len;
        int err = read_u64(fp, &len);
        if (err) return err;
        return skip_bytes(fp, len);
    } else if (type == GGUF_TYPE_ARRAY) {
        uint32_t elem_type;
        uint64_t count;
        int err = read_u32(fp, &elem_type);
        if (err) return err;
        err = read_u64(fp, &count);
        if (err) return err;
        for (uint64_t i = 0; i < count; i++) {
            err = skip_gguf_value(fp, elem_type, depth + 1);
            if (err) return err;
        }
        return GGUF_OK;
    } else {
        size_t sz = gguf_value_type_size(type);
        if (sz == 0) return GGUF_ERR_FORMAT;
        return skip_bytes(fp, sz);
    }
}

/* Read a single KV pair */
static int read_kv_pair(FILE *fp, gguf_kv_t *kv)
{
    int err;

    /* Read key string */
    err = read_gguf_string(fp, kv->key, sizeof(kv->key), NULL);
    if (err) return err;

    /* Read value type */
    err = read_u32(fp, &kv->type);
    if (err) return err;

    /* Read value based on type */
    switch (kv->type) {
        case GGUF_TYPE_UINT8:   return read_u8(fp, &kv->value.u8);
        case GGUF_TYPE_INT8:    return read_i8(fp, &kv->value.i8);
        case GGUF_TYPE_UINT16:  return read_u16(fp, &kv->value.u16);
        case GGUF_TYPE_INT16:   return read_i16(fp, &kv->value.i16);
        case GGUF_TYPE_UINT32:  return read_u32(fp, &kv->value.u32);
        case GGUF_TYPE_INT32:   return read_i32(fp, &kv->value.i32);
        case GGUF_TYPE_FLOAT32: return read_f32(fp, &kv->value.f32);
        case GGUF_TYPE_BOOL:    return read_u8(fp, &kv->value.u8);  /* bool stored as u8 */
        case GGUF_TYPE_UINT64:  return read_u64(fp, &kv->value.u64);
        case GGUF_TYPE_INT64:   return read_i64(fp, &kv->value.i64);
        case GGUF_TYPE_FLOAT64: return read_f64(fp, &kv->value.f64);
        case GGUF_TYPE_STRING:
            return read_gguf_string(fp, kv->value.s.str, sizeof(kv->value.s.str), &kv->value.s.len);
        case GGUF_TYPE_ARRAY:
            /* Read array header but skip the data */
            err = read_u32(fp, &kv->value.arr.elem_type);
            if (err) return err;
            err = read_u64(fp, &kv->value.arr.count);
            if (err) return err;
            /* Skip array elements */
            for (uint64_t i = 0; i < kv->value.arr.count; i++) {
                err = skip_gguf_value(fp, kv->value.arr.elem_type, 0);
                if (err) return err;
            }
            return GGUF_OK;
        default:
            return GGUF_ERR_FORMAT;
    }
}

/* ---- Tensor Info Parsing ---- */

static int read_tensor_info(FILE *fp, gguf_tensor_info_t *t)
{
    int err;

    /* Name */
    err = read_gguf_string(fp, t->name, sizeof(t->name), NULL);
    if (err) return err;

    /* Number of dimensions */
    err = read_u32(fp, &t->n_dims);
    if (err) return err;

    if (t->n_dims > GGUF_MAX_DIMS)
        return GGUF_ERR_FORMAT;

    /* Dimensions — SEC-003: check for overflow */
    t->n_elements = 1;
    for (uint32_t i = 0; i < t->n_dims; i++) {
        err = read_u64(fp, &t->dims[i]);
        if (err) return err;
        if (t->dims[i] > 0 && t->n_elements > UINT64_MAX / t->dims[i])
            return GGUF_ERR_OVERFLOW;
        t->n_elements *= t->dims[i];
    }
    /* Zero unused dims */
    for (uint32_t i = t->n_dims; i < GGUF_MAX_DIMS; i++)
        t->dims[i] = 0;

    /* Tensor type */
    err = read_u32(fp, &t->type);
    if (err) return err;

    /* Data offset (relative to tensor data block start) */
    err = read_u64(fp, &t->offset);
    if (err) return err;

    /* Compute byte size */
    t->n_bytes = ggml_tensor_bytes(t->type, t->n_elements);

    return GGUF_OK;
}

/* ---- Internal: shared GGUF parsing (called after ctx->fp is set) ---- */

static int parse_gguf(gguf_ctx_t *ctx)
{
    int err;

    /* Read header */
    uint32_t magic;
    err = read_u32(ctx->fp, &magic);
    if (err) return err;

    if (magic != GGUF_MAGIC)
        return GGUF_ERR_MAGIC;

    err = read_u32(ctx->fp, &ctx->version);
    if (err) return err;

    if (ctx->version < 2 || ctx->version > 3)
        return GGUF_ERR_VERSION;

    err = read_u64(ctx->fp, &ctx->n_tensors);
    if (err) return err;

    err = read_u64(ctx->fp, &ctx->n_kv);
    if (err) return err;

    /* SEC-008: Reject excessive counts to prevent memory exhaustion */
    if (ctx->n_tensors > GGUF_MAX_TENSORS) return GGUF_ERR_FORMAT;
    if (ctx->n_kv > GGUF_MAX_KV_PAIRS) return GGUF_ERR_FORMAT;

    /* Allocate KV array */
    if (ctx->n_kv > 0) {
        ctx->kv = (gguf_kv_t *)calloc((size_t)ctx->n_kv, sizeof(gguf_kv_t));
        if (!ctx->kv)
            return GGUF_ERR_ALLOC;
    }

    /* Read KV pairs */
    ctx->alignment = GGUF_DEFAULT_ALIGN;
    for (uint64_t i = 0; i < ctx->n_kv; i++) {
        err = read_kv_pair(ctx->fp, &ctx->kv[i]);
        if (err) return err;

        /* Check for alignment override */
        if (ctx->kv[i].type == GGUF_TYPE_UINT32 &&
            strcmp(ctx->kv[i].key, "general.alignment") == 0) {
            ctx->alignment = ctx->kv[i].value.u32;
        }
    }

    /* Allocate tensor info array */
    if (ctx->n_tensors > 0) {
        ctx->tensors = (gguf_tensor_info_t *)calloc((size_t)ctx->n_tensors,
                                                     sizeof(gguf_tensor_info_t));
        if (!ctx->tensors)
            return GGUF_ERR_ALLOC;
    }

    /* Read tensor info */
    for (uint64_t i = 0; i < ctx->n_tensors; i++) {
        err = read_tensor_info(ctx->fp, &ctx->tensors[i]);
        if (err) return err;
    }

    /* Calculate tensor data offset: current position, aligned up */
    long pos = ftell(ctx->fp);
    if (pos < 0)
        return GGUF_ERR_READ;

    uint64_t align = ctx->alignment;
    ctx->data_offset = ((uint64_t)pos + align - 1) & ~(align - 1);

    return GGUF_OK;
}

/* ---- Public API ---- */

int gguf_open(gguf_ctx_t *ctx, const char *path)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->fp = fopen(path, "rb");
    if (!ctx->fp)
        return GGUF_ERR_OPEN;

    int err = parse_gguf(ctx);
    if (err) {
        gguf_close(ctx);
        return err;
    }
    return GGUF_OK;
}

int gguf_open_memory(gguf_ctx_t *ctx, const void *data, size_t len)
{
    memset(ctx, 0, sizeof(*ctx));

    if (!data || len == 0)
        return GGUF_ERR_OPEN;

    ctx->fp = fmemopen((void *)data, len, "rb");
    if (!ctx->fp)
        return GGUF_ERR_OPEN;

    int err = parse_gguf(ctx);
    if (err) {
        gguf_close(ctx);
        return err;
    }
    return GGUF_OK;
}

void gguf_close(gguf_ctx_t *ctx)
{
    if (ctx->kv) {
        free(ctx->kv);
        ctx->kv = NULL;
    }
    if (ctx->tensors) {
        free(ctx->tensors);
        ctx->tensors = NULL;
    }
    if (ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
}

uint64_t gguf_get_n_tensors(const gguf_ctx_t *ctx)
{
    return ctx->n_tensors;
}

const gguf_tensor_info_t *gguf_get_tensor_info(const gguf_ctx_t *ctx, uint64_t index)
{
    if (index >= ctx->n_tensors) return NULL;
    return &ctx->tensors[index];
}

const gguf_tensor_info_t *gguf_find_tensor(const gguf_ctx_t *ctx, const char *name)
{
    for (uint64_t i = 0; i < ctx->n_tensors; i++) {
        if (strcmp(ctx->tensors[i].name, name) == 0)
            return &ctx->tensors[i];
    }
    return NULL;
}

int gguf_read_tensor_data(gguf_ctx_t *ctx, const gguf_tensor_info_t *tensor, void *buf)
{
    if (!ctx->fp) return GGUF_ERR_OPEN;

    uint64_t abs_offset = ctx->data_offset + tensor->offset;
    if (fseek(ctx->fp, (long)abs_offset, SEEK_SET) != 0)
        return GGUF_ERR_READ;

    return read_exact(ctx->fp, buf, (size_t)tensor->n_bytes);
}

/* ---- KV Lookup Helpers ---- */

static const gguf_kv_t *find_kv(const gguf_ctx_t *ctx, const char *key)
{
    return gguf_find_kv(ctx, key);
}

const gguf_kv_t *gguf_find_kv(const gguf_ctx_t *ctx, const char *key)
{
    for (uint64_t i = 0; i < ctx->n_kv; i++) {
        if (strcmp(ctx->kv[i].key, key) == 0)
            return &ctx->kv[i];
    }
    return NULL;
}

const char *gguf_get_kv_string(const gguf_ctx_t *ctx, const char *key)
{
    const gguf_kv_t *kv = find_kv(ctx, key);
    if (!kv || kv->type != GGUF_TYPE_STRING) return NULL;
    return kv->value.s.str;
}

bool gguf_get_kv_u32(const gguf_ctx_t *ctx, const char *key, uint32_t *out)
{
    const gguf_kv_t *kv = find_kv(ctx, key);
    if (!kv || kv->type != GGUF_TYPE_UINT32) return false;
    *out = kv->value.u32;
    return true;
}

bool gguf_get_kv_i32(const gguf_ctx_t *ctx, const char *key, int32_t *out)
{
    const gguf_kv_t *kv = find_kv(ctx, key);
    if (!kv || kv->type != GGUF_TYPE_INT32) return false;
    *out = kv->value.i32;
    return true;
}

bool gguf_get_kv_f32(const gguf_ctx_t *ctx, const char *key, float *out)
{
    const gguf_kv_t *kv = find_kv(ctx, key);
    if (!kv || kv->type != GGUF_TYPE_FLOAT32) return false;
    *out = kv->value.f32;
    return true;
}

bool gguf_get_kv_u64(const gguf_ctx_t *ctx, const char *key, uint64_t *out)
{
    const gguf_kv_t *kv = find_kv(ctx, key);
    if (!kv || kv->type != GGUF_TYPE_UINT64) return false;
    *out = kv->value.u64;
    return true;
}

/* ---- Utility ---- */

const char *gguf_strerror(int err)
{
    switch (err) {
        case GGUF_OK:           return "OK";
        case GGUF_ERR_OPEN:     return "cannot open file";
        case GGUF_ERR_READ:     return "read error / truncated file";
        case GGUF_ERR_MAGIC:    return "bad magic number (not a GGUF file)";
        case GGUF_ERR_VERSION:  return "unsupported GGUF version";
        case GGUF_ERR_ALLOC:    return "memory allocation failed";
        case GGUF_ERR_OVERFLOW: return "value too large for buffer";
        case GGUF_ERR_FORMAT:   return "malformed data";
        default:                return "unknown error";
    }
}

const char *gguf_type_name(uint32_t type)
{
    switch (type) {
        case GGML_TYPE_F32:     return "F32";
        case GGML_TYPE_F16:     return "F16";
        case GGML_TYPE_Q4_0:    return "Q4_0";
        case GGML_TYPE_Q4_1:    return "Q4_1";
        case GGML_TYPE_Q5_0:    return "Q5_0";
        case GGML_TYPE_Q5_1:    return "Q5_1";
        case GGML_TYPE_Q8_0:    return "Q8_0";
        case GGML_TYPE_Q8_1:    return "Q8_1";
        case GGML_TYPE_Q2_K:    return "Q2_K";
        case GGML_TYPE_Q3_K:    return "Q3_K";
        case GGML_TYPE_Q4_K:    return "Q4_K";
        case GGML_TYPE_Q5_K:    return "Q5_K";
        case GGML_TYPE_Q6_K:    return "Q6_K";
        case GGML_TYPE_Q8_K:    return "Q8_K";
        case GGML_TYPE_I8:      return "I8";
        case GGML_TYPE_I16:     return "I16";
        case GGML_TYPE_I32:     return "I32";
        case GGML_TYPE_I64:     return "I64";
        case GGML_TYPE_F64:     return "F64";
        default:                return "unknown";
    }
}
