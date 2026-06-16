/*
 * JARVIS AI-OS Phase 3 — GGUF Parser Test Suite
 *
 * Self-contained tests using embedded GGUF binary data.
 * No external .gguf files needed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "gguf_parser.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("Test %d: %s ... ", tests_run, name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    return; \
} while(0)

#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

/* ---- Helper: Write bytes to a temp GGUF file ---- */

/* Write a GGUF string (uint64 len + bytes) to buffer, return bytes written */
static size_t put_gguf_string(uint8_t *buf, const char *s)
{
    uint64_t len = strlen(s);
    memcpy(buf, &len, 8);
    memcpy(buf + 8, s, len);
    return 8 + len;
}

/* Build a minimal GGUF v3 file with 2 KV pairs and 2 tensors, return size */
static size_t build_test_gguf(uint8_t *buf, size_t buf_size)
{
    uint8_t *p = buf;
    memset(buf, 0, buf_size);

    /* Header: magic(u32) version(u32) n_tensors(u64) n_kv(u64) */
    uint32_t magic   = GGUF_MAGIC;
    uint32_t version = 3;
    uint64_t n_tensors = 2;
    uint64_t n_kv      = 3;

    memcpy(p, &magic, 4);   p += 4;
    memcpy(p, &version, 4); p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8);     p += 8;

    /* KV 1: "general.name" = string "test-model" */
    p += put_gguf_string(p, "general.name");
    uint32_t type_str = GGUF_TYPE_STRING;
    memcpy(p, &type_str, 4); p += 4;
    p += put_gguf_string(p, "test-model");

    /* KV 2: "general.alignment" = uint32 64 */
    p += put_gguf_string(p, "general.alignment");
    uint32_t type_u32 = GGUF_TYPE_UINT32;
    memcpy(p, &type_u32, 4); p += 4;
    uint32_t align_val = 64;
    memcpy(p, &align_val, 4); p += 4;

    /* KV 3: "test.float_val" = float32 3.14 */
    p += put_gguf_string(p, "test.float_val");
    uint32_t type_f32 = GGUF_TYPE_FLOAT32;
    memcpy(p, &type_f32, 4); p += 4;
    float fval = 3.14f;
    memcpy(p, &fval, 4); p += 4;

    /* Tensor 1: "weight.embed" — 2D F32, 4x8 = 32 elements = 128 bytes */
    p += put_gguf_string(p, "weight.embed");
    uint32_t ndims1 = 2;
    memcpy(p, &ndims1, 4); p += 4;
    uint64_t dim1a = 4, dim1b = 8;
    memcpy(p, &dim1a, 8); p += 8;
    memcpy(p, &dim1b, 8); p += 8;
    uint32_t type_f32t = GGML_TYPE_F32;
    memcpy(p, &type_f32t, 4); p += 4;
    uint64_t offset1 = 0;
    memcpy(p, &offset1, 8); p += 8;

    /* Tensor 2: "weight.output" — 1D Q4_0, 64 elements */
    /* Q4_0: block_size=32, type_size=18. 64 elems = 2 blocks = 36 bytes */
    p += put_gguf_string(p, "weight.output");
    uint32_t ndims2 = 1;
    memcpy(p, &ndims2, 4); p += 4;
    uint64_t dim2a = 64;
    memcpy(p, &dim2a, 8); p += 8;
    uint32_t type_q4 = GGML_TYPE_Q4_0;
    memcpy(p, &type_q4, 4); p += 4;
    uint64_t offset2 = 128;  /* After first tensor's 128 bytes */
    memcpy(p, &offset2, 8); p += 8;

    /* Align to 64 bytes for tensor data (alignment KV says 64) */
    size_t header_end = (size_t)(p - buf);
    size_t data_start = (header_end + 63) & ~(size_t)63;

    /* Write some fake tensor data */
    /* Tensor 1: 128 bytes of 0x41 */
    memset(buf + data_start, 0x41, 128);
    /* Tensor 2: 36 bytes of 0x42 */
    memset(buf + data_start + 128, 0x42, 36);

    return data_start + 128 + 36;
}

/* Write buffer to temp file, return path */
static const char *write_temp_gguf(const uint8_t *data, size_t size)
{
    static char path[256];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\jarvis_test.gguf", getenv("TEMP") ? getenv("TEMP") : ".");
#else
    snprintf(path, sizeof(path), "/tmp/jarvis_test.gguf");
#endif
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(data, 1, size, f);
    fclose(f);
    return path;
}

/* ---- Tests ---- */

static void test_header_parsing(void)
{
    TEST("Header parsing (magic, version, counts)");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);
    ASSERT(path, "failed to write temp file");

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(ctx.version == 3, "wrong version");
    ASSERT(ctx.n_tensors == 2, "wrong tensor count");
    ASSERT(ctx.n_kv == 3, "wrong kv count");
    gguf_close(&ctx);
    PASS();
}

static void test_kv_string(void)
{
    TEST("KV metadata: string value");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const char *name = gguf_get_kv_string(&ctx, "general.name");
    ASSERT(name != NULL, "general.name not found");
    ASSERT(strcmp(name, "test-model") == 0, "wrong name value");

    gguf_close(&ctx);
    PASS();
}

static void test_kv_numeric(void)
{
    TEST("KV metadata: uint32 and float32 values");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    uint32_t align;
    ASSERT(gguf_get_kv_u32(&ctx, "general.alignment", &align), "alignment not found");
    ASSERT(align == 64, "wrong alignment");
    ASSERT(ctx.alignment == 64, "alignment not applied to context");

    float fval;
    ASSERT(gguf_get_kv_f32(&ctx, "test.float_val", &fval), "float not found");
    ASSERT(fval > 3.13f && fval < 3.15f, "wrong float value");

    /* Missing key */
    ASSERT(!gguf_get_kv_u32(&ctx, "nonexistent.key", &align), "found nonexistent key");

    gguf_close(&ctx);
    PASS();
}

static void test_tensor_info(void)
{
    TEST("Tensor info parsing (name, dims, type, offset)");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    ASSERT(gguf_get_n_tensors(&ctx) == 2, "wrong tensor count");

    /* Tensor 0: weight.embed */
    const gguf_tensor_info_t *t0 = gguf_get_tensor_info(&ctx, 0);
    ASSERT(t0 != NULL, "tensor 0 null");
    ASSERT(strcmp(t0->name, "weight.embed") == 0, "wrong tensor 0 name");
    ASSERT(t0->n_dims == 2, "wrong dims count");
    ASSERT(t0->dims[0] == 4, "wrong dim 0");
    ASSERT(t0->dims[1] == 8, "wrong dim 1");
    ASSERT(t0->type == GGML_TYPE_F32, "wrong type");
    ASSERT(t0->n_elements == 32, "wrong n_elements");
    ASSERT(t0->n_bytes == 128, "wrong n_bytes (32 * 4 = 128)");
    ASSERT(t0->offset == 0, "wrong offset");

    /* Tensor 1: weight.output */
    const gguf_tensor_info_t *t1 = gguf_get_tensor_info(&ctx, 1);
    ASSERT(t1 != NULL, "tensor 1 null");
    ASSERT(strcmp(t1->name, "weight.output") == 0, "wrong tensor 1 name");
    ASSERT(t1->n_dims == 1, "wrong dims");
    ASSERT(t1->dims[0] == 64, "wrong dim");
    ASSERT(t1->type == GGML_TYPE_Q4_0, "wrong type");
    ASSERT(t1->n_elements == 64, "wrong n_elements");
    ASSERT(t1->n_bytes == 36, "wrong n_bytes (2 blocks * 18 = 36)");

    /* Find by name */
    const gguf_tensor_info_t *found = gguf_find_tensor(&ctx, "weight.output");
    ASSERT(found == t1, "find_tensor returned wrong pointer");

    ASSERT(gguf_find_tensor(&ctx, "nonexistent") == NULL, "found nonexistent tensor");

    gguf_close(&ctx);
    PASS();
}

static void test_data_alignment(void)
{
    TEST("Data alignment calculation");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    /* data_offset should be aligned to 64 (from general.alignment KV) */
    ASSERT((ctx.data_offset % 64) == 0, "data_offset not aligned to 64");
    ASSERT(ctx.data_offset > 0, "data_offset is zero");

    gguf_close(&ctx);
    PASS();
}

static void test_tensor_data_read(void)
{
    TEST("Tensor data read");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const gguf_tensor_info_t *t0 = gguf_get_tensor_info(&ctx, 0);
    uint8_t buf[256];
    err = gguf_read_tensor_data(&ctx, t0, buf);
    ASSERT(err == GGUF_OK, "failed to read tensor data");

    /* We wrote 0x41 for tensor 0 data */
    ASSERT(buf[0] == 0x41, "wrong tensor data byte 0");
    ASSERT(buf[127] == 0x41, "wrong tensor data byte 127");

    /* Tensor 1 data */
    const gguf_tensor_info_t *t1 = gguf_get_tensor_info(&ctx, 1);
    err = gguf_read_tensor_data(&ctx, t1, buf);
    ASSERT(err == GGUF_OK, "failed to read tensor 1 data");
    ASSERT(buf[0] == 0x42, "wrong tensor 1 data byte 0");
    ASSERT(buf[35] == 0x42, "wrong tensor 1 data byte 35");

    gguf_close(&ctx);
    PASS();
}

static void test_error_handling(void)
{
    TEST("Error handling (bad magic, missing file, bad version)");

    gguf_ctx_t ctx;

    /* Missing file */
    int err = gguf_open(&ctx, "/tmp/nonexistent_jarvis_test.gguf");
    ASSERT(err == GGUF_ERR_OPEN, "expected OPEN error");

    /* Bad magic */
    uint8_t bad_magic[32] = {0};
    bad_magic[0] = 0xFF;  /* Not GGUF magic */
    const char *path = write_temp_gguf(bad_magic, sizeof(bad_magic));
    err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_MAGIC, "expected MAGIC error");

    /* Bad version (version 99) */
    uint8_t bad_ver[32] = {0};
    uint32_t magic = GGUF_MAGIC;
    uint32_t ver = 99;
    memcpy(bad_ver, &magic, 4);
    memcpy(bad_ver + 4, &ver, 4);
    /* Need enough data for n_tensors + n_kv */
    uint64_t zero = 0;
    memcpy(bad_ver + 8, &zero, 8);
    memcpy(bad_ver + 16, &zero, 8);
    path = write_temp_gguf(bad_ver, 24);
    err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_VERSION, "expected VERSION error");

    /* Truncated file */
    uint8_t truncated[8];
    memcpy(truncated, &magic, 4);
    uint32_t v3 = 3;
    memcpy(truncated + 4, &v3, 4);
    /* File ends here — no n_tensors/n_kv */
    path = write_temp_gguf(truncated, 8);
    err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_READ, "expected READ error for truncated file");

    /* Error string */
    ASSERT(strcmp(gguf_strerror(GGUF_ERR_MAGIC), "bad magic number (not a GGUF file)") == 0,
           "wrong error string");

    PASS();
}

static void test_type_names(void)
{
    TEST("Type name lookup");

    ASSERT(strcmp(gguf_type_name(GGML_TYPE_F32), "F32") == 0, "F32 name wrong");
    ASSERT(strcmp(gguf_type_name(GGML_TYPE_Q4_0), "Q4_0") == 0, "Q4_0 name wrong");
    ASSERT(strcmp(gguf_type_name(GGML_TYPE_Q4_K), "Q4_K") == 0, "Q4_K name wrong");
    ASSERT(strcmp(gguf_type_name(GGML_TYPE_Q8_0), "Q8_0") == 0, "Q8_0 name wrong");
    ASSERT(strcmp(gguf_type_name(999), "unknown") == 0, "unknown type name wrong");

    PASS();
}

static void test_kv_missing_type(void)
{
    TEST("KV lookup with wrong type returns false/NULL");

    uint8_t data[8192];
    size_t sz = build_test_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    /* general.name is a string — asking for u32 should fail */
    uint32_t val;
    ASSERT(!gguf_get_kv_u32(&ctx, "general.name", &val), "should fail: string as u32");

    /* general.alignment is u32 — asking for string should fail */
    ASSERT(gguf_get_kv_string(&ctx, "general.alignment") == NULL, "should fail: u32 as string");

    gguf_close(&ctx);
    PASS();
}

/* SEC-003: Test dimension overflow detection */
static void test_overflow_detection(void)
{
    TEST("SEC-003: Tensor dimension overflow detection");

    uint8_t data[8192];
    uint8_t *p = data;
    memset(data, 0, sizeof(data));

    /* Header */
    uint32_t magic = GGUF_MAGIC, version = 3;
    uint64_t n_tensors = 1, n_kv = 0;
    memcpy(p, &magic, 4); p += 4;
    memcpy(p, &version, 4); p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8); p += 8;

    /* Tensor with overflowing dims: [2^33, 2^33] — product = 2^66, overflows uint64 */
    uint64_t name_len = 4;
    memcpy(p, &name_len, 8); p += 8;
    memcpy(p, "test", 4); p += 4;
    uint32_t ndims = 2;
    memcpy(p, &ndims, 4); p += 4;
    uint64_t dim_huge = (1ULL << 33);  /* 8589934592 */
    memcpy(p, &dim_huge, 8); p += 8;
    memcpy(p, &dim_huge, 8); p += 8;
    uint32_t type = 0;
    memcpy(p, &type, 4); p += 4;
    uint64_t offset = 0;
    memcpy(p, &offset, 8); p += 8;

    const char *path = write_temp_gguf(data, (size_t)(p - data));
    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_OVERFLOW, "expected OVERFLOW error for huge dims");
    PASS();
}

/* SEC-008: Test excessive n_kv rejection */
static void test_excessive_kv(void)
{
    TEST("SEC-008: Excessive n_kv rejection");

    uint8_t data[32];
    uint8_t *p = data;
    uint32_t magic = GGUF_MAGIC, version = 3;
    uint64_t n_tensors = 0, n_kv = 100000;
    memcpy(p, &magic, 4); p += 4;
    memcpy(p, &version, 4); p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8); p += 8;

    const char *path = write_temp_gguf(data, (size_t)(p - data));
    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_FORMAT, "expected FORMAT error for excessive n_kv");
    PASS();
}

/* SEC-008: Test excessive n_tensors rejection */
static void test_excessive_tensors(void)
{
    TEST("SEC-008: Excessive n_tensors rejection");

    uint8_t data[32];
    uint8_t *p = data;
    uint32_t magic = GGUF_MAGIC, version = 3;
    uint64_t n_tensors = 100000, n_kv = 0;
    memcpy(p, &magic, 4); p += 4;
    memcpy(p, &version, 4); p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8); p += 8;

    const char *path = write_temp_gguf(data, (size_t)(p - data));
    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_ERR_FORMAT, "expected FORMAT error for excessive n_tensors");
    PASS();
}

/* Build a GGUF with 0 tensors and 2 array KV pairs:
 *   "test.ffn"    = u32[6]  { 6144,6144,6144,12288,12288,12288 }
 *   "test.floats" = f32[2]  { 1.0, 2.0 }   (non-integer — must be rejected) */
static size_t build_array_gguf(uint8_t *buf, size_t buf_size)
{
    uint8_t *p = buf;
    memset(buf, 0, buf_size);

    uint32_t magic = GGUF_MAGIC, version = 3;
    uint64_t n_tensors = 0, n_kv = 2;
    memcpy(p, &magic, 4);     p += 4;
    memcpy(p, &version, 4);   p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8);      p += 8;

    /* KV 1: "test.ffn" = u32 array */
    p += put_gguf_string(p, "test.ffn");
    uint32_t type_arr = GGUF_TYPE_ARRAY;
    memcpy(p, &type_arr, 4); p += 4;
    uint32_t elem_u32 = GGUF_TYPE_UINT32;
    memcpy(p, &elem_u32, 4); p += 4;
    uint64_t cnt = 6;
    memcpy(p, &cnt, 8); p += 8;
    uint32_t ffn_vals[6] = { 6144, 6144, 6144, 12288, 12288, 12288 };
    for (int i = 0; i < 6; i++) { memcpy(p, &ffn_vals[i], 4); p += 4; }

    /* KV 2: "test.floats" = f32 array */
    p += put_gguf_string(p, "test.floats");
    memcpy(p, &type_arr, 4); p += 4;
    uint32_t elem_f32 = GGUF_TYPE_FLOAT32;
    memcpy(p, &elem_f32, 4); p += 4;
    uint64_t cnt2 = 2;
    memcpy(p, &cnt2, 8); p += 8;
    float fvals[2] = { 1.0f, 2.0f };
    for (int i = 0; i < 2; i++) { memcpy(p, &fvals[i], 4); p += 4; }

    return (size_t)(p - buf);
}

static void test_kv_array_u32(void)
{
    TEST("KV metadata: numeric array extraction (gguf_get_kv_array_u32)");

    uint8_t data[8192];
    size_t sz = build_array_gguf(data, sizeof(data));
    const char *path = write_temp_gguf(data, sz);
    ASSERT(path, "failed to write temp file");

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(ctx.n_kv == 2, "wrong kv count");

    /* Count-only query (out=NULL, max=0) */
    size_t count = 0;
    ASSERT(gguf_get_kv_array_u32(&ctx, "test.ffn", NULL, 0, &count), "count-only query failed");
    ASSERT(count == 6, "wrong array count");

    /* Full read */
    uint32_t out[6] = {0};
    count = 0;
    ASSERT(gguf_get_kv_array_u32(&ctx, "test.ffn", out, 6, &count), "full read failed");
    ASSERT(count == 6, "wrong full-read count");
    ASSERT(out[0] == 6144 && out[2] == 6144, "wrong early FFN values");
    ASSERT(out[3] == 12288 && out[5] == 12288, "wrong late FFN values");

    /* Truncated read: max=3 still reports full count, reads first 3 */
    uint32_t out3[3] = {0};
    count = 0;
    ASSERT(gguf_get_kv_array_u32(&ctx, "test.ffn", out3, 3, &count), "truncated read failed");
    ASSERT(count == 6, "truncated read should report full count");
    ASSERT(out3[0] == 6144 && out3[1] == 6144 && out3[2] == 6144, "wrong truncated values");

    /* Non-integer (float) array must be rejected */
    ASSERT(!gguf_get_kv_array_u32(&ctx, "test.floats", out, 6, &count), "float array should be rejected");

    /* Missing key must be rejected */
    ASSERT(!gguf_get_kv_array_u32(&ctx, "nonexistent.arr", out, 6, &count), "missing key should be rejected");

    gguf_close(&ctx);
    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS GGUF Parser Tests ===\n\n");

    test_header_parsing();
    test_kv_string();
    test_kv_numeric();
    test_kv_array_u32();
    test_tensor_info();
    test_data_alignment();
    test_tensor_data_read();
    test_error_handling();
    test_type_names();
    test_kv_missing_type();
    test_overflow_detection();
    test_excessive_kv();
    test_excessive_tensors();

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);

    /* Cleanup temp file */
#ifdef _WIN32
    char path[256];
    snprintf(path, sizeof(path), "%s\\jarvis_test.gguf", getenv("TEMP") ? getenv("TEMP") : ".");
    remove(path);
#else
    remove("/tmp/jarvis_test.gguf");
#endif

    return (tests_passed == tests_run) ? 0 : 1;
}
