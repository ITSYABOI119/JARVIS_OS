/*
 * JARVIS AI-OS — Tensor Operations Test Suite
 * Verifies all C tensor ops for transformer inference.
 */

#include <stdio.h>
#include <math.h>
#include "tensor_ops.h"

static int pass = 0, fail = 0, total = 0;
#define EPS 1e-3f
#define CLOSE(a, b) (fabsf((a) - (b)) < EPS)

#define TEST(name) do { total++; printf("Test %d: %s ... ", total, name); } while(0)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL_MSG(msg) do { fail++; printf("FAIL: %s\n", msg); } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL_MSG(msg); } while(0)

/* ---- Tests ---- */

static void test_add(void)
{
    TEST("add: [1,2,3] + [4,5,6] = [5,7,9]");
    float a[] = {1,2,3}, b[] = {4,5,6}, out[3];
    tensor_add(a, b, out, 3);
    CHECK(CLOSE(out[0],5) && CLOSE(out[1],7) && CLOSE(out[2],9), "wrong result");
}

static void test_mul(void)
{
    TEST("mul: [2,3] * [4,5] = [8,15]");
    float a[] = {2,3}, b[] = {4,5}, out[2];
    tensor_mul(a, b, out, 2);
    CHECK(CLOSE(out[0],8) && CLOSE(out[1],15), "wrong result");
}

static void test_scale(void)
{
    TEST("scale: [1,2,3] * 2.0 = [2,4,6]");
    float a[] = {1,2,3}, out[3];
    tensor_scale(a, 2.0f, out, 3);
    CHECK(CLOSE(out[0],2) && CLOSE(out[1],4) && CLOSE(out[2],6), "wrong result");
}

static void test_matmul_2x2(void)
{
    TEST("matmul 2x2: [[1,2],[3,4]] * [[5,6],[7,8]] = [[19,22],[43,50]]");
    float a[] = {1,2,3,4}, b[] = {5,6,7,8}, out[4];
    tensor_matmul(a, b, out, 2, 2, 2);
    CHECK(CLOSE(out[0],19) && CLOSE(out[1],22) && CLOSE(out[2],43) && CLOSE(out[3],50),
          "wrong 2x2 result");
}

static void test_matmul_identity(void)
{
    TEST("matmul identity: A * I = A (3x3)");
    float a[] = {1,2,3, 4,5,6, 7,8,9};
    float I[] = {1,0,0, 0,1,0, 0,0,1};
    float out[9];
    tensor_matmul(a, I, out, 3, 3, 3);
    int ok = 1;
    for (int i = 0; i < 9; i++) if (!CLOSE(out[i], a[i])) ok = 0;
    CHECK(ok, "A*I != A");
}

static void test_matmul_non_square(void)
{
    TEST("matmul non-square: [2x3] * [3x2] = [2x2]");
    float a[] = {1,2,3, 4,5,6};     /* 2x3 */
    float b[] = {7,8, 9,10, 11,12}; /* 3x2 */
    float out[4];
    tensor_matmul(a, b, out, 2, 3, 2);
    /* [1*7+2*9+3*11, 1*8+2*10+3*12] = [58, 64] */
    /* [4*7+5*9+6*11, 4*8+5*10+6*12] = [139, 154] */
    CHECK(CLOSE(out[0],58) && CLOSE(out[1],64) && CLOSE(out[2],139) && CLOSE(out[3],154),
          "wrong non-square result");
}

static void test_relu(void)
{
    TEST("relu: [-1, 0, 1, 2] -> [0, 0, 1, 2]");
    float in[] = {-1, 0, 1, 2}, out[4];
    tensor_relu(in, out, 4);
    CHECK(CLOSE(out[0],0) && CLOSE(out[1],0) && CLOSE(out[2],1) && CLOSE(out[3],2),
          "wrong relu result");
}

static void test_gelu(void)
{
    TEST("gelu: GELU(0)~0, GELU(1)~0.841, GELU(-1)~-0.159");
    float in[] = {0.0f, 1.0f, -1.0f}, out[3];
    tensor_gelu(in, out, 3);
    CHECK(CLOSE(out[0], 0.0f) && CLOSE(out[1], 0.841f) && CLOSE(out[2], -0.159f),
          "wrong gelu values");
}

static void test_silu(void)
{
    TEST("silu: SiLU(0)=0, SiLU(1)~0.731, SiLU(-1)~-0.269");
    float in[] = {0.0f, 1.0f, -1.0f}, out[3];
    tensor_silu(in, out, 3);
    CHECK(CLOSE(out[0], 0.0f) && CLOSE(out[1], 0.731f) && CLOSE(out[2], -0.269f),
          "wrong silu values");
}

static void test_rms_norm(void)
{
    TEST("rms_norm: [1,2,3,4] with unit weights, eps=1e-5");
    float in[] = {1, 2, 3, 4};
    float w[]  = {1, 1, 1, 1};
    float out[4];
    tensor_rms_norm(in, w, out, 4, 1e-5f);
    /* rms = sqrt((1+4+9+16)/4) = sqrt(7.5) ~ 2.7386 */
    float rms = sqrtf(7.5f);
    int ok = 1;
    for (int i = 0; i < 4; i++)
        if (!CLOSE(out[i], in[i] / rms)) ok = 0;
    CHECK(ok, "wrong rms_norm result");
}

static void test_softmax(void)
{
    TEST("softmax: [1,2,3] -> [0.090, 0.245, 0.665], sum=1.0");
    float in[] = {1, 2, 3}, out[3];
    tensor_softmax(in, out, 3);
    float sum = out[0] + out[1] + out[2];
    CHECK(CLOSE(sum, 1.0f) && CLOSE(out[0], 0.090f) && CLOSE(out[1], 0.245f) && CLOSE(out[2], 0.665f),
          "wrong softmax values");
}

static void test_softmax_stability(void)
{
    TEST("softmax stability: [1000, 1001, 1002] -> no NaN, sum=1.0");
    float in[] = {1000, 1001, 1002}, out[3];
    tensor_softmax(in, out, 3);
    float sum = out[0] + out[1] + out[2];
    int ok = !isnan(sum) && !isinf(sum) && CLOSE(sum, 1.0f);
    CHECK(ok, "NaN/Inf or sum != 1.0");
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS Tensor Operations Tests ===\n\n");

    test_add();
    test_mul();
    test_scale();
    test_matmul_2x2();
    test_matmul_identity();
    test_matmul_non_square();
    test_relu();
    test_gelu();
    test_silu();
    test_rms_norm();
    test_softmax();
    test_softmax_stability();

    printf("\n=== Results: %d/%d PASS ===\n", pass, total);
    return (pass == total) ? 0 : 1;
}
