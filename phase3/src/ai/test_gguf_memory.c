/*
 * JARVIS AI-OS -- gguf_open_memory() Test Suite
 *
 * Tests memory-buffer GGUF parsing using the real Llama 3.2 1B model file.
 * Also re-tests gguf_open() (file path) to prove the refactoring is safe.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static const char *MODEL_PATH =
    "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf";

/* Read entire file into malloc'd buffer. Returns NULL on failure. */
static void *read_file_to_buffer(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    void *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(buf); return NULL; }

    *out_len = (size_t)len;
    return buf;
}

/* ---- Tests ---- */

static void *model_buf = NULL;
static size_t model_len = 0;

static void test_file_open(void)
{
    TEST("gguf_open() file path — still works after refactoring");

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, MODEL_PATH);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(ctx.version == 3, "expected version 3");
    ASSERT(ctx.n_tensors == 147, "expected 147 tensors");
    ASSERT(ctx.n_kv == 35, "expected 35 KV pairs");
    gguf_close(&ctx);
    PASS();
}

static void test_memory_open(void)
{
    TEST("gguf_open_memory() — basic parse from buffer");

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, model_buf, model_len);
    ASSERT(err == GGUF_OK, gguf_strerror(err));
    ASSERT(ctx.version == 3, "expected version 3");
    ASSERT(ctx.n_tensors == 147, "expected 147 tensors");
    ASSERT(ctx.n_kv == 35, "expected 35 KV pairs");
    gguf_close(&ctx);
    PASS();
}

static void test_memory_tensor_info(void)
{
    TEST("gguf_open_memory() — first tensor name found");

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, model_buf, model_len);
    ASSERT(err == GGUF_OK, gguf_strerror(err));

    const gguf_tensor_info_t *t0 = gguf_get_tensor_info(&ctx, 0);
    ASSERT(t0 != NULL, "tensor 0 is NULL");
    ASSERT(strlen(t0->name) > 0, "tensor 0 name is empty");
    printf("(tensor 0: \"%s\") ", t0->name);

    /* Verify we can also find a tensor by name */
    const gguf_tensor_info_t *found = gguf_find_tensor(&ctx, t0->name);
    ASSERT(found != NULL, "find_tensor failed for tensor 0 name");
    ASSERT(found == t0, "find_tensor returned different pointer");

    gguf_close(&ctx);
    PASS();
}

static void test_memory_kv_metadata(void)
{
    TEST("gguf_open_memory() — KV metadata matches file open");

    gguf_ctx_t ctx_file, ctx_mem;
    int err;

    err = gguf_open(&ctx_file, MODEL_PATH);
    ASSERT(err == GGUF_OK, "file open failed");

    err = gguf_open_memory(&ctx_mem, model_buf, model_len);
    ASSERT(err == GGUF_OK, "memory open failed");

    /* Compare KV counts */
    ASSERT(ctx_file.n_kv == ctx_mem.n_kv, "n_kv mismatch");

    /* Compare a few KV values */
    const char *arch_f = gguf_get_kv_string(&ctx_file, "general.architecture");
    const char *arch_m = gguf_get_kv_string(&ctx_mem, "general.architecture");
    ASSERT(arch_f && arch_m, "architecture key missing");
    ASSERT(strcmp(arch_f, arch_m) == 0, "architecture mismatch");

    gguf_close(&ctx_file);
    gguf_close(&ctx_mem);
    PASS();
}

static void test_memory_tensor_data_read(void)
{
    TEST("gguf_open_memory() — tensor data read matches file open");

    gguf_ctx_t ctx_file, ctx_mem;
    int err;

    err = gguf_open(&ctx_file, MODEL_PATH);
    ASSERT(err == GGUF_OK, "file open failed");

    err = gguf_open_memory(&ctx_mem, model_buf, model_len);
    ASSERT(err == GGUF_OK, "memory open failed");

    /* Read the first tensor data from both and compare first 64 bytes */
    const gguf_tensor_info_t *t0_f = gguf_get_tensor_info(&ctx_file, 0);
    const gguf_tensor_info_t *t0_m = gguf_get_tensor_info(&ctx_mem, 0);
    ASSERT(t0_f && t0_m, "tensor 0 null");
    ASSERT(t0_f->n_bytes == t0_m->n_bytes, "tensor byte sizes differ");

    /* Read just the first min(64, n_bytes) bytes for a quick compare */
    size_t cmp_bytes = t0_f->n_bytes < 64 ? (size_t)t0_f->n_bytes : 64;
    uint8_t *buf_f = (uint8_t *)malloc(cmp_bytes);
    uint8_t *buf_m = (uint8_t *)malloc(cmp_bytes);
    ASSERT(buf_f && buf_m, "alloc failed");

    /* For the comparison we need to read full tensor; just read first chunk */
    uint8_t *full_f = (uint8_t *)malloc((size_t)t0_f->n_bytes);
    uint8_t *full_m = (uint8_t *)malloc((size_t)t0_m->n_bytes);
    ASSERT(full_f && full_m, "alloc full tensor failed");

    err = gguf_read_tensor_data(&ctx_file, t0_f, full_f);
    ASSERT(err == GGUF_OK, "file tensor read failed");

    err = gguf_read_tensor_data(&ctx_mem, t0_m, full_m);
    ASSERT(err == GGUF_OK, "memory tensor read failed");

    ASSERT(memcmp(full_f, full_m, cmp_bytes) == 0, "tensor data mismatch");

    free(full_f); free(full_m);
    free(buf_f); free(buf_m);
    gguf_close(&ctx_file);
    gguf_close(&ctx_mem);
    PASS();
}

static void test_memory_null_input(void)
{
    TEST("gguf_open_memory() — NULL data returns error");

    gguf_ctx_t ctx;
    int err = gguf_open_memory(&ctx, NULL, 100);
    ASSERT(err != GGUF_OK, "expected error for NULL data");

    err = gguf_open_memory(&ctx, model_buf, 0);
    ASSERT(err != GGUF_OK, "expected error for zero length");

    PASS();
}

static void test_memory_truncated(void)
{
    TEST("gguf_open_memory() — truncated buffer returns error");

    gguf_ctx_t ctx;
    /* Only 8 bytes — enough for magic + version, but not n_tensors */
    int err = gguf_open_memory(&ctx, model_buf, 8);
    ASSERT(err != GGUF_OK, "expected error for truncated buffer");

    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS gguf_open_memory() Test Suite ===\n\n");

    /* Load model file into memory once */
    model_buf = read_file_to_buffer(MODEL_PATH, &model_len);
    if (!model_buf) {
        printf("SKIP: Cannot read model file: %s\n", MODEL_PATH);
        printf("(This test requires the real Llama 3.2 1B GGUF model)\n");
        return 0;
    }
    printf("Loaded model: %zu bytes (%.1f MB)\n\n", model_len, model_len / (1024.0 * 1024.0));

    test_file_open();
    test_memory_open();
    test_memory_tensor_info();
    test_memory_kv_metadata();
    test_memory_tensor_data_read();
    test_memory_null_input();
    test_memory_truncated();

    free(model_buf);

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
