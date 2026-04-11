/*
 * JARVIS AI-OS Phase 3 — C-Only GGUF File Parser
 *
 * Pure C11 parser for GGUF model files. Replaces C++ gguf.cpp
 * for seL4 bare-metal userspace (no STL, no exceptions, no mmap).
 *
 * GGUF Spec: https://github.com/ggml-org/ggml/blob/master/docs/gguf.md
 * Format: Little-endian, strings are length-prefixed (uint64 + bytes, NOT null-terminated)
 */

#ifndef GGUF_PARSER_H
#define GGUF_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* ---- Constants ---- */

#define GGUF_MAGIC          0x46554747  /* "GGUF" as little-endian u32: G(47) G(47) U(55) F(46) */
#define GGUF_VERSION_3      3
#define GGUF_DEFAULT_ALIGN  32

#define GGUF_MAX_NAME_LEN      256
#define GGUF_MAX_KV_KEY_LEN    256
#define GGUF_MAX_DIMS          4
#define GGUF_MAX_KV_STR_LEN    4096
#define GGUF_MAX_ARRAY_DEPTH   8      /* SEC-007: max nested array recursion */
#define GGUF_MAX_KV_PAIRS      65536  /* SEC-008: max metadata entries */
#define GGUF_MAX_TENSORS        65536  /* SEC-008: max tensor count */

/* ---- GGUF Metadata Value Types ---- */

typedef enum {
    GGUF_TYPE_UINT8   = 0,
    GGUF_TYPE_INT8    = 1,
    GGUF_TYPE_UINT16  = 2,
    GGUF_TYPE_INT16   = 3,
    GGUF_TYPE_UINT32  = 4,
    GGUF_TYPE_INT32   = 5,
    GGUF_TYPE_FLOAT32 = 6,
    GGUF_TYPE_BOOL    = 7,
    GGUF_TYPE_STRING  = 8,
    GGUF_TYPE_ARRAY   = 9,
    GGUF_TYPE_UINT64  = 10,
    GGUF_TYPE_INT64   = 11,
    GGUF_TYPE_FLOAT64 = 12,
} gguf_value_type_t;

/* ---- ggml Tensor Types (must match ggml.h enum ggml_type) ---- */

typedef enum {
    GGML_TYPE_F32     = 0,
    GGML_TYPE_F16     = 1,
    GGML_TYPE_Q4_0    = 2,
    GGML_TYPE_Q4_1    = 3,
    /* 4 unused */
    GGML_TYPE_Q5_0    = 6,
    GGML_TYPE_Q5_1    = 7,
    GGML_TYPE_Q8_0    = 8,
    GGML_TYPE_Q8_1    = 9,
    GGML_TYPE_Q2_K    = 10,
    GGML_TYPE_Q3_K    = 11,
    GGML_TYPE_Q4_K    = 12,
    GGML_TYPE_Q5_K    = 13,
    GGML_TYPE_Q6_K    = 14,
    GGML_TYPE_Q8_K    = 15,
    GGML_TYPE_IQ2_XXS = 16,
    GGML_TYPE_IQ2_XS  = 17,
    GGML_TYPE_IQ3_XXS = 18,
    GGML_TYPE_IQ1_S   = 19,
    GGML_TYPE_IQ4_NL  = 20,
    GGML_TYPE_IQ3_S   = 21,
    GGML_TYPE_IQ2_S   = 22,
    GGML_TYPE_IQ4_XS  = 23,
    GGML_TYPE_I8      = 24,
    GGML_TYPE_I16     = 25,
    GGML_TYPE_I32     = 26,
    GGML_TYPE_I64     = 27,
    GGML_TYPE_F64     = 28,
    GGML_TYPE_IQ1_M   = 29,
    GGML_TYPE_BF16    = 30,  /* bfloat16 — 2 bytes, upper 16 bits of F32 */
    GGML_TYPE_COUNT,
} ggml_type_t;

/* ---- Error Codes ---- */

typedef enum {
    GGUF_OK             =  0,
    GGUF_ERR_OPEN       = -1,  /* Cannot open file */
    GGUF_ERR_READ       = -2,  /* Read error / truncated file */
    GGUF_ERR_MAGIC      = -3,  /* Bad magic number */
    GGUF_ERR_VERSION    = -4,  /* Unsupported version */
    GGUF_ERR_ALLOC      = -5,  /* Memory allocation failed */
    GGUF_ERR_OVERFLOW   = -6,  /* Value too large for buffer */
    GGUF_ERR_FORMAT     = -7,  /* Malformed data */
} gguf_error_t;

/* ---- Metadata KV Pair ---- */

typedef struct {
    char     key[GGUF_MAX_KV_KEY_LEN];
    uint32_t type;      /* gguf_value_type_t */

    /* Value storage — only one field is valid, determined by type */
    union {
        uint8_t  u8;
        int8_t   i8;
        uint16_t u16;
        int16_t  i16;
        uint32_t u32;
        int32_t  i32;
        float    f32;
        bool     b;
        uint64_t u64;
        int64_t  i64;
        double   f64;
        struct {
            uint64_t len;
            char     str[GGUF_MAX_KV_STR_LEN];
        } s;
        /* Arrays: we store type + count but skip the data (too variable) */
        struct {
            uint32_t elem_type;
            uint64_t count;
        } arr;
    } value;
} gguf_kv_t;

/* ---- Tensor Info ---- */

typedef struct {
    char       name[GGUF_MAX_NAME_LEN];
    uint32_t   n_dims;
    uint64_t   dims[GGUF_MAX_DIMS];
    uint32_t   type;       /* ggml_type_t */
    uint64_t   offset;     /* Offset relative to tensor data start */
    uint64_t   n_elements; /* Computed: product of dims */
    uint64_t   n_bytes;    /* Computed: size in bytes for this tensor */
} gguf_tensor_info_t;

/* ---- GGUF Context (parsed file) ---- */

typedef struct {
    /* Header */
    uint32_t version;
    uint64_t n_tensors;
    uint64_t n_kv;

    /* Alignment (from general.alignment metadata, default 32) */
    uint32_t alignment;

    /* Offset where tensor data begins in the file */
    uint64_t data_offset;

    /* Parsed metadata */
    gguf_kv_t          *kv;       /* Array of n_kv entries (malloc'd) */
    gguf_tensor_info_t *tensors;  /* Array of n_tensors entries (malloc'd) */

    /* File handle (kept open for tensor data reads) */
    FILE *fp;
} gguf_ctx_t;

/* ---- API ---- */

/**
 * Open and parse a GGUF file. Reads header, metadata, and tensor info.
 * Does NOT read tensor data — use gguf_read_tensor_data() for that.
 * Returns GGUF_OK on success, negative error code on failure.
 */
int gguf_open(gguf_ctx_t *ctx, const char *path);

/**
 * Open and parse GGUF data from a memory buffer.
 * Uses fmemopen() internally — same parsing as gguf_open().
 * The caller must keep 'data' alive until gguf_close().
 * Returns GGUF_OK on success, negative error code on failure.
 */
int gguf_open_memory(gguf_ctx_t *ctx, const void *data, size_t len);

/**
 * Close a GGUF context and free resources.
 */
void gguf_close(gguf_ctx_t *ctx);

/**
 * Get number of tensors in the model.
 */
uint64_t gguf_get_n_tensors(const gguf_ctx_t *ctx);

/**
 * Get tensor info by index.
 * Returns NULL if index out of range.
 */
const gguf_tensor_info_t *gguf_get_tensor_info(const gguf_ctx_t *ctx, uint64_t index);

/**
 * Find tensor info by name.
 * Returns NULL if not found.
 */
const gguf_tensor_info_t *gguf_find_tensor(const gguf_ctx_t *ctx, const char *name);

/**
 * Read tensor data from file into caller-provided buffer.
 * buf must be at least tensor->n_bytes large.
 * Returns GGUF_OK on success.
 */
int gguf_read_tensor_data(gguf_ctx_t *ctx, const gguf_tensor_info_t *tensor, void *buf);

/**
 * Find a metadata KV pair by key.
 * Returns pointer to the KV entry, or NULL if not found.
 */
const gguf_kv_t *gguf_find_kv(const gguf_ctx_t *ctx, const char *key);

/**
 * Find a metadata string value by key.
 * Returns pointer to string value, or NULL if not found / wrong type.
 */
const char *gguf_get_kv_string(const gguf_ctx_t *ctx, const char *key);

/**
 * Find a metadata uint32 value by key.
 * Returns true and sets *out if found, false otherwise.
 */
bool gguf_get_kv_u32(const gguf_ctx_t *ctx, const char *key, uint32_t *out);

/**
 * Find a metadata int32 value by key.
 */
bool gguf_get_kv_i32(const gguf_ctx_t *ctx, const char *key, int32_t *out);

/**
 * Find a metadata float32 value by key.
 */
bool gguf_get_kv_f32(const gguf_ctx_t *ctx, const char *key, float *out);

/**
 * Find a metadata uint64 value by key.
 */
bool gguf_get_kv_u64(const gguf_ctx_t *ctx, const char *key, uint64_t *out);

/**
 * Get human-readable error message for error code.
 */
const char *gguf_strerror(int err);

/**
 * Get human-readable name for ggml tensor type.
 */
const char *gguf_type_name(uint32_t type);

#endif /* GGUF_PARSER_H */
