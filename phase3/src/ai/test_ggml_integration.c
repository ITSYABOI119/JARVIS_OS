/*
 * JARVIS AI-OS -- ggml Integration Test
 * Tests that ggml core library links and runs with our POSIX stubs.
 *
 * What this tests:
 *   1. ggml context creation (ggml_init)
 *   2. Tensor allocation (ggml_new_tensor_1d)
 *   3. Direct data access via ggml_get_data_f32()
 *   4. Graph node creation (ggml_add) - builds graph, no compute
 *   5. Context cleanup (ggml_free)
 *
 * What this does NOT test (requires ggml-cpu backend):
 *   - ggml_graph_compute (actual tensor math execution)
 *   - ggml_set_f32 / ggml_get_f32_1d (moved to ggml-cpu.c)
 *
 * Compile:
 *   gcc -O2 -std=c11 -DNDEBUG -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE \
 *       -I<ggml>/include -I<ggml>/src \
 *       test_ggml_integration.c ggml.o ggml-alloc.o ggml-quants.o \
 *       -lm -o test_ggml_int && ./test_ggml_int
 */

#include "ggml.h"
#include <stdio.h>
#include <string.h>

static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name, cond) do { \
    if (cond) { printf("  PASS: %s\n", name); tests_pass++; } \
    else      { printf("  FAIL: %s\n", name); tests_fail++; } \
} while(0)

int main(void)
{
    printf("=== ggml Integration Test ===\n\n");

    /* Test 1: Context creation */
    struct ggml_init_params params = {
        .mem_size   = 16 * 1024 * 1024,  /* 16 MB */
        .mem_buffer = NULL,
        .no_alloc   = 0,
    };
    struct ggml_context *ctx = ggml_init(params);
    TEST("ggml_init returns non-NULL context", ctx != NULL);

    /* Test 2: Tensor allocation */
    struct ggml_tensor *a = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 4);
    TEST("ggml_new_tensor_1d returns non-NULL", a != NULL);
    TEST("tensor has correct element count", ggml_nelements(a) == 4);
    TEST("tensor has correct byte size", ggml_nbytes(a) == 4 * sizeof(float));

    /* Test 3: Direct data access */
    float *a_data = ggml_get_data_f32(a);
    TEST("ggml_get_data_f32 returns non-NULL", a_data != NULL);

    /* Write values directly to tensor data */
    a_data[0] = 1.0f;
    a_data[1] = 2.0f;
    a_data[2] = 3.0f;
    a_data[3] = 4.0f;
    TEST("direct data write (a[0]=1.0)", a_data[0] == 1.0f);
    TEST("direct data write (a[3]=4.0)", a_data[3] == 4.0f);

    /* Test 4: Second tensor */
    struct ggml_tensor *b = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 4);
    float *b_data = ggml_get_data_f32(b);
    b_data[0] = 10.0f;
    b_data[1] = 20.0f;
    b_data[2] = 30.0f;
    b_data[3] = 40.0f;
    TEST("second tensor allocation", b != NULL && b_data != NULL);

    /* Test 5: Graph node creation (ggml_add builds a graph node, no compute) */
    struct ggml_tensor *c = ggml_add(ctx, a, b);
    TEST("ggml_add returns non-NULL graph node", c != NULL);
    TEST("ggml_add result has correct element count", ggml_nelements(c) == 4);
    TEST("ggml_add result op is GGML_OP_ADD", c->op == GGML_OP_ADD);
    TEST("ggml_add src[0] is tensor a", c->src[0] == a);
    TEST("ggml_add src[1] is tensor b", c->src[1] == b);

    /* Test 6: Type info functions */
    TEST("ggml_type_name(F32) is 'f32'", strcmp(ggml_type_name(GGML_TYPE_F32), "f32") == 0);
    TEST("ggml_type_size(F32) is 4", ggml_type_size(GGML_TYPE_F32) == 4);

    /* Test 7: Cleanup */
    ggml_free(ctx);
    TEST("ggml_free completed (no crash)", 1);

    printf("\n=== Results: %d PASS, %d FAIL ===\n",
           tests_pass, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
