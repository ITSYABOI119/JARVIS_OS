/*
 * JARVIS AI-OS — End-to-End GGUF Loading Test
 *
 * Proves the pipeline: synthetic GGUF file → gguf_parser → tensor data verified.
 *
 * Two modes:
 *   Standalone (default): Tests GGUF parse + tensor data read. No ggml dependency.
 *   ggml mode (-DWITH_GGML): Additionally loads into ggml tensors. Requires ggml objects.
 *
 * Compile (standalone — goes in CI):
 *   gcc -Wall -Werror -O2 -std=c11 test_gguf_to_ggml.c gguf_parser.c -I. -o test_gguf_load
 *
 * Compile (with ggml — local verification):
 *   gcc -O2 -std=c11 -DWITH_GGML -I<ggml>/include test_gguf_to_ggml.c gguf_parser.c \
 *       ggml.o ggml-alloc.o ggml-quants.o -lm -o test_gguf_load_full
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "gguf_parser.h"

#ifdef WITH_GGML
#include "ggml.h"
#endif

/* ---- Test framework ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

/* ---- Synthetic GGUF File Builder ---- */

/* Helper: write a GGUF string (uint64 len + bytes) */
static size_t put_str(uint8_t *buf, const char *s)
{
    uint64_t len = (uint64_t)strlen(s);
    memcpy(buf, &len, 8);
    memcpy(buf + 8, s, len);
    return 8 + (size_t)len;
}

/*
 * Build a synthetic GGUF v3 file containing:
 *   - 2 KV pairs: general.architecture="test", general.name="jarvis-test-model"
 *   - 1 tensor: "test.weight", F32, dims=[4,4], data=1.0,2.0,...,16.0
 *
 * Returns total file size. Writes to buf (must be >= 4096 bytes).
 */
static size_t build_synthetic_gguf(uint8_t *buf, size_t buf_size)
{
    uint8_t *p = buf;
    memset(buf, 0, buf_size);

    /* Header: magic(u32) version(u32) n_tensors(u64) n_kv(u64) */
    uint32_t magic   = GGUF_MAGIC;
    uint32_t version = 3;
    uint64_t n_tensors = 1;
    uint64_t n_kv      = 2;

    memcpy(p, &magic, 4);     p += 4;
    memcpy(p, &version, 4);   p += 4;
    memcpy(p, &n_tensors, 8); p += 8;
    memcpy(p, &n_kv, 8);      p += 8;

    /* KV 1: "general.architecture" = string "test" */
    p += put_str(p, "general.architecture");
    uint32_t type_str = GGUF_TYPE_STRING;
    memcpy(p, &type_str, 4); p += 4;
    p += put_str(p, "test");

    /* KV 2: "general.name" = string "jarvis-test-model" */
    p += put_str(p, "general.name");
    memcpy(p, &type_str, 4); p += 4;
    p += put_str(p, "jarvis-test-model");

    /* Tensor: "test.weight", 2D F32 [4,4] = 16 elements = 64 bytes */
    p += put_str(p, "test.weight");
    uint32_t ndims = 2;
    memcpy(p, &ndims, 4); p += 4;
    uint64_t dim0 = 4, dim1 = 4;
    memcpy(p, &dim0, 8); p += 8;
    memcpy(p, &dim1, 8); p += 8;
    uint32_t type_f32 = GGML_TYPE_F32;
    memcpy(p, &type_f32, 4); p += 4;
    uint64_t offset = 0;  /* Offset relative to tensor data start */
    memcpy(p, &offset, 8); p += 8;

    /* Align to GGUF_DEFAULT_ALIGN (32 bytes) for tensor data */
    size_t header_end = (size_t)(p - buf);
    size_t data_start = (header_end + GGUF_DEFAULT_ALIGN - 1) & ~(size_t)(GGUF_DEFAULT_ALIGN - 1);

    /* Tensor data: 16 floats with values 1.0 through 16.0 */
    float *fdata = (float *)(buf + data_start);
    for (int i = 0; i < 16; i++) {
        fdata[i] = (float)(i + 1);  /* 1.0, 2.0, 3.0, ... 16.0 */
    }

    return data_start + 64;  /* 16 floats * 4 bytes = 64 */
}

/* Write buffer to temp file */
static const char *write_temp_file(const uint8_t *data, size_t size)
{
#ifdef _WIN32
    static char path[256];
    snprintf(path, sizeof(path), "%s\\jarvis_e2e_test.gguf",
             getenv("TEMP") ? getenv("TEMP") : ".");
#else
    static const char *path = "/tmp/jarvis_e2e_test.gguf";
#endif
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(data, 1, size, f);
    fclose(f);
    return path;
}

/* ---- Tests ---- */

static void test_gguf_file_generation(void)
{
    TEST("Synthetic GGUF file generation");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    ASSERT(sz > 0, "build returned 0");
    ASSERT(sz < sizeof(data), "build exceeded buffer");

    /* Verify magic at offset 0 */
    uint32_t magic;
    memcpy(&magic, data, 4);
    ASSERT(magic == GGUF_MAGIC, "wrong magic in generated file");

    PASS();
}

static void test_parse_header(void)
{
    TEST("Parse synthetic GGUF header");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);
    ASSERT(path, "failed to write temp file");

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(ctx.version == 3, "wrong version");
    ASSERT(ctx.n_kv == 2, "wrong n_kv");
    ASSERT(ctx.n_tensors == 1, "wrong n_tensors");

    gguf_close(&ctx);
    PASS();
}

static void test_parse_kv_architecture(void)
{
    TEST("Parse KV: general.architecture == 'test'");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const char *arch = gguf_get_kv_string(&ctx, "general.architecture");
    ASSERT(arch != NULL, "general.architecture not found");
    ASSERT(strcmp(arch, "test") == 0, "wrong architecture value");

    gguf_close(&ctx);
    PASS();
}

static void test_parse_kv_name(void)
{
    TEST("Parse KV: general.name == 'jarvis-test-model'");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const char *name = gguf_get_kv_string(&ctx, "general.name");
    ASSERT(name != NULL, "general.name not found");
    ASSERT(strcmp(name, "jarvis-test-model") == 0, "wrong name value");

    gguf_close(&ctx);
    PASS();
}

static void test_parse_tensor_info(void)
{
    TEST("Parse tensor info: test.weight [4,4] F32");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(gguf_get_n_tensors(&ctx) == 1, "wrong tensor count");

    const gguf_tensor_info_t *t = gguf_get_tensor_info(&ctx, 0);
    ASSERT(t != NULL, "tensor 0 is NULL");
    ASSERT(strcmp(t->name, "test.weight") == 0, "wrong tensor name");
    ASSERT(t->n_dims == 2, "wrong n_dims");
    ASSERT(t->dims[0] == 4, "wrong dim[0]");
    ASSERT(t->dims[1] == 4, "wrong dim[1]");
    ASSERT(t->type == GGML_TYPE_F32, "wrong type");
    ASSERT(t->n_elements == 16, "wrong n_elements");
    ASSERT(t->n_bytes == 64, "wrong n_bytes (16 * 4)");

    gguf_close(&ctx);
    PASS();
}

static void test_find_tensor_by_name(void)
{
    TEST("Find tensor by name: gguf_find_tensor('test.weight')");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const gguf_tensor_info_t *t = gguf_find_tensor(&ctx, "test.weight");
    ASSERT(t != NULL, "tensor not found by name");
    ASSERT(t->n_elements == 16, "wrong n_elements");

    const gguf_tensor_info_t *missing = gguf_find_tensor(&ctx, "nonexistent");
    ASSERT(missing == NULL, "found nonexistent tensor");

    gguf_close(&ctx);
    PASS();
}

static void test_read_tensor_data(void)
{
    TEST("Read tensor data: verify 16 float values (1.0 through 16.0)");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const gguf_tensor_info_t *t = gguf_find_tensor(&ctx, "test.weight");
    ASSERT(t != NULL, "tensor not found");

    /* Read raw tensor data */
    float weights[16];
    err = gguf_read_tensor_data(&ctx, t, weights);
    ASSERT(err == GGUF_OK, "failed to read tensor data");

    /* Verify all 16 values */
    int all_ok = 1;
    for (int i = 0; i < 16; i++) {
        float expected = (float)(i + 1);
        if (fabsf(weights[i] - expected) > 0.001f) {
            printf("FAIL: weights[%d] = %f, expected %f\n", i, weights[i], expected);
            all_ok = 0;
        }
    }
    ASSERT(all_ok, "tensor data mismatch");

    gguf_close(&ctx);
    PASS();
}

static void test_data_alignment(void)
{
    TEST("Tensor data alignment (default 32 bytes)");

    uint8_t data[4096];
    size_t sz = build_synthetic_gguf(data, sizeof(data));
    const char *path = write_temp_file(data, sz);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    ASSERT(ctx.data_offset > 0, "data_offset is 0");
    ASSERT((ctx.data_offset % GGUF_DEFAULT_ALIGN) == 0,
           "data_offset not aligned to 32 bytes");

    gguf_close(&ctx);
    PASS();
}

static void test_full_pipeline(void)
{
    TEST("Full pipeline: generate → parse → read → verify (end-to-end)");

    /* Step 1: Generate synthetic GGUF */
    uint8_t file_data[4096];
    size_t sz = build_synthetic_gguf(file_data, sizeof(file_data));
    const char *path = write_temp_file(file_data, sz);
    ASSERT(path, "failed to write GGUF file");

    /* Step 2: Parse with gguf_parser */
    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    /* Step 3: Verify metadata */
    const char *arch = gguf_get_kv_string(&ctx, "general.architecture");
    ASSERT(arch && strcmp(arch, "test") == 0, "wrong architecture");

    const char *name = gguf_get_kv_string(&ctx, "general.name");
    ASSERT(name && strcmp(name, "jarvis-test-model") == 0, "wrong name");

    /* Step 4: Get tensor info */
    const gguf_tensor_info_t *t = gguf_find_tensor(&ctx, "test.weight");
    ASSERT(t != NULL, "tensor not found");
    ASSERT(t->n_dims == 2 && t->dims[0] == 4 && t->dims[1] == 4, "wrong dims");
    ASSERT(t->type == GGML_TYPE_F32, "wrong type");

    /* Step 5: Read raw tensor weights */
    float weights[16];
    err = gguf_read_tensor_data(&ctx, t, weights);
    ASSERT(err == GGUF_OK, "data read failed");

    /* Step 6: Verify data integrity */
    ASSERT(fabsf(weights[0] - 1.0f) < 0.001f, "weights[0] != 1.0");
    ASSERT(fabsf(weights[7] - 8.0f) < 0.001f, "weights[7] != 8.0");
    ASSERT(fabsf(weights[15] - 16.0f) < 0.001f, "weights[15] != 16.0");

#ifdef WITH_GGML
    /* Step 7 (ggml mode): Load into ggml tensor */
    struct ggml_init_params params = {
        .mem_size   = 1024 * 1024,
        .mem_buffer = NULL,
        .no_alloc   = 0,
    };
    struct ggml_context *gctx = ggml_init(params);
    ASSERT(gctx != NULL, "ggml_init failed");

    struct ggml_tensor *tensor = ggml_new_tensor_2d(gctx, GGML_TYPE_F32, 4, 4);
    ASSERT(tensor != NULL, "ggml_new_tensor_2d failed");

    /* Copy GGUF data into ggml tensor */
    float *tdata = ggml_get_data_f32(tensor);
    memcpy(tdata, weights, 64);

    /* Verify via ggml accessors */
    ASSERT(fabsf(tdata[0] - 1.0f) < 0.001f, "ggml tensor[0] != 1.0");
    ASSERT(fabsf(tdata[15] - 16.0f) < 0.001f, "ggml tensor[15] != 16.0");

    ggml_free(gctx);
#endif

    gguf_close(&ctx);
    PASS();
}

/* ---- Main ---- */

int main(void)
{
#ifdef WITH_GGML
    printf("=== GGUF-to-ggml End-to-End Loading Test (with ggml) ===\n\n");
#else
    printf("=== GGUF-to-ggml End-to-End Loading Test (standalone) ===\n\n");
#endif

    test_gguf_file_generation();
    test_parse_header();
    test_parse_kv_architecture();
    test_parse_kv_name();
    test_parse_tensor_info();
    test_find_tensor_by_name();
    test_read_tensor_data();
    test_data_alignment();
    test_full_pipeline();

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);

    /* Cleanup */
#ifdef _WIN32
    char cleanup[256];
    snprintf(cleanup, sizeof(cleanup), "%s\\jarvis_e2e_test.gguf",
             getenv("TEMP") ? getenv("TEMP") : ".");
    remove(cleanup);
#else
    remove("/tmp/jarvis_e2e_test.gguf");
#endif

    return (tests_passed == tests_run) ? 0 : 1;
}
