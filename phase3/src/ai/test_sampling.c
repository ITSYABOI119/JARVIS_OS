#include <stdio.h>
#include <math.h>
#include "sampling.h"

static int pass = 0, fail = 0, total = 0;
#define EPS 1e-3f
#define TEST(name) do { total++; printf("Test %d: %s ... ", total, name); } while(0)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL_MSG(m) do { fail++; printf("FAIL: %s\n", m); } while(0)
#define CHECK(c, m) do { if (c) PASS(); else FAIL_MSG(m); } while(0)

/* Test 1: greedy picks obvious maximum */
static void test_greedy_obvious(void)
{
    TEST("greedy_obvious");
    float logits[] = {0, 0, 10, 0, 0};
    int idx = sample_greedy(logits, 5);
    CHECK(idx == 2, "expected index 2");
}

/* Test 2: greedy with all negative values */
static void test_greedy_negative(void)
{
    TEST("greedy_negative");
    float logits[] = {-5, -3, -1, -10};
    int idx = sample_greedy(logits, 4);
    CHECK(idx == 2, "expected index 2 (least negative)");
}

/* Test 3: greedy tie-breaking favors first */
static void test_greedy_first_wins(void)
{
    TEST("greedy_first_wins");
    float logits[] = {5, 5, 5};
    int idx = sample_greedy(logits, 3);
    CHECK(idx == 0, "expected index 0 (first wins tie)");
}

/* Test 4: topk with k=1 is deterministic argmax */
static void test_topk_deterministic(void)
{
    TEST("topk_deterministic");
    float logits[] = {0, 0, 100, 0, 0};
    uint64_t seed = 42;
    int idx = sample_topk(logits, 5, 1, 1.0f, &seed);
    CHECK(idx == 2, "expected index 2 with k=1");
}

/* Test 5: low temperature concentrates on max */
static void test_topk_temperature_low(void)
{
    TEST("topk_temperature_low");
    float logits[] = {0, 5, 10, 3};
    uint64_t seed = 12345;
    int count_max = 0;
    for (int i = 0; i < 100; i++) {
        int idx = sample_topk(logits, 4, 3, 0.01f, &seed);
        if (idx == 2) count_max++;
    }
    CHECK(count_max >= 95, "expected >=95/100 picks of index 2 at temp=0.01");
}

/* Test 6: uniform logits with high temp produce all indices */
static void test_topk_distribution(void)
{
    TEST("topk_distribution");
    float logits[] = {0, 0, 0, 0, 0};
    uint64_t seed = 9876543;
    int counts[5] = {0};
    for (int i = 0; i < 1000; i++) {
        int idx = sample_topk(logits, 5, 5, 1.0f, &seed);
        if (idx >= 0 && idx < 5) counts[idx]++;
    }
    int all_seen = 1;
    for (int i = 0; i < 5; i++) {
        if (counts[i] == 0) { all_seen = 0; break; }
    }
    CHECK(all_seen, "all 5 indices should appear at least once in 1000 samples");
}

/* Test 7: RNG is deterministic for same seed */
static void test_rng_deterministic(void)
{
    TEST("rng_deterministic");
    uint64_t s1 = 0xDEADBEEF;
    uint64_t s2 = 0xDEADBEEF;
    int match = 1;
    for (int i = 0; i < 100; i++) {
        if (sampling_rng_next(&s1) != sampling_rng_next(&s2)) {
            match = 0;
            break;
        }
    }
    CHECK(match, "same seed must produce identical sequence");
}

/* Test 8: RNG floats in [0,1) with mean near 0.5 */
static void test_rng_distribution(void)
{
    TEST("rng_distribution");
    uint64_t seed = 777;
    double sum = 0.0;
    int in_range = 1;
    for (int i = 0; i < 1000; i++) {
        float f = sampling_rng_float(&seed);
        if (f < 0.0f || f >= 1.0f) { in_range = 0; break; }
        sum += f;
    }
    double mean = sum / 1000.0;
    CHECK(in_range && fabs(mean - 0.5) < 0.1,
          "1000 floats should be in [0,1) with mean ~0.5");
}

/* Test 9: argmax_prob returns correct index and probability */
static void test_argmax_prob(void)
{
    TEST("argmax_prob");
    float logits[] = {1, 2, 3};
    float prob = 0.0f;
    int idx = sample_argmax_prob(logits, 3, &prob);
    /* softmax([1,2,3])[2] = e^3/(e^1+e^2+e^3) = e^0/(e^-2+e^-1+e^0) */
    /* = 1.0 / (0.1353 + 0.3679 + 1.0) = 1.0 / 1.5032 = 0.6652 */
    float expected = 1.0f / (expf(-2.0f) + expf(-1.0f) + 1.0f);
    CHECK(idx == 2 && fabsf(prob - expected) < EPS,
          "expected index 2, prob ~0.665");
}

int main(void)
{
    printf("=== Sampling Tests ===\n");

    test_greedy_obvious();
    test_greedy_negative();
    test_greedy_first_wins();
    test_topk_deterministic();
    test_topk_temperature_low();
    test_topk_distribution();
    test_rng_deterministic();
    test_rng_distribution();
    test_argmax_prob();

    printf("\n%d/%d PASS, %d FAIL\n", pass, total, fail);
    return fail ? 1 : 0;
}
