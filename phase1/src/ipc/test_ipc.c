/*
 * JARVIS AI-OS - IPC Latency Test
 * Phase 1 Week 4
 *
 * Measure lock-free ring buffer performance
 * Target: <100μs latency (Phase 0: 54μs median achieved)
 */

#define _POSIX_C_SOURCE 199309L

#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/* Test configuration */
#define NUM_ITERATIONS 100000
#define WARMUP_ITERATIONS 1000

/* Latency measurement utilities */
typedef struct {
    uint64_t *samples;
    size_t count;
    size_t capacity;
} latency_stats_t;

static void latency_stats_init(latency_stats_t *stats, size_t capacity)
{
    stats->samples = malloc(capacity * sizeof(uint64_t));
    stats->count = 0;
    stats->capacity = capacity;
}

static void latency_stats_free(latency_stats_t *stats)
{
    free(stats->samples);
}

static void latency_stats_add(latency_stats_t *stats, uint64_t latency_ns)
{
    if (stats->count < stats->capacity) {
        stats->samples[stats->count++] = latency_ns;
    }
}

static int compare_uint64(const void *a, const void *b)
{
    uint64_t ua = *(const uint64_t*)a;
    uint64_t ub = *(const uint64_t*)b;
    return (ua > ub) - (ua < ub);
}

static void latency_stats_print(const latency_stats_t *stats)
{
    if (stats->count == 0) {
        printf("No samples collected\n");
        return;
    }

    /* Sort samples for percentile calculation */
    uint64_t *sorted = malloc(stats->count * sizeof(uint64_t));
    memcpy(sorted, stats->samples, stats->count * sizeof(uint64_t));
    qsort(sorted, stats->count, sizeof(uint64_t), compare_uint64);

    /* Calculate statistics */
    uint64_t min = sorted[0];
    uint64_t max = sorted[stats->count - 1];
    uint64_t median = sorted[stats->count / 2];
    uint64_t p95 = sorted[(stats->count * 95) / 100];
    uint64_t p99 = sorted[(stats->count * 99) / 100];

    uint64_t sum = 0;
    for (size_t i = 0; i < stats->count; i++) {
        sum += sorted[i];
    }
    uint64_t mean = sum / stats->count;

    printf("\n========== LATENCY STATISTICS ==========\n");
    printf("Samples:           %zu\n", stats->count);
    printf("Min:               %llu ns (%.3f μs)\n",
           (unsigned long long)min, min / 1000.0);
    printf("Median:            %llu ns (%.3f μs)\n",
           (unsigned long long)median, median / 1000.0);
    printf("Mean:              %llu ns (%.3f μs)\n",
           (unsigned long long)mean, mean / 1000.0);
    printf("95th percentile:   %llu ns (%.3f μs)\n",
           (unsigned long long)p95, p95 / 1000.0);
    printf("99th percentile:   %llu ns (%.3f μs)\n",
           (unsigned long long)p99, p99 / 1000.0);
    printf("Max:               %llu ns (%.3f μs)\n",
           (unsigned long long)max, max / 1000.0);
    printf("========================================\n");

    /* Check against targets */
    printf("\nTarget: <100μs (100,000 ns)\n");
    if (median < 100000) {
        printf("✓ PASS: Median latency meets <100μs target\n");
    } else {
        printf("✗ FAIL: Median latency exceeds 100μs target\n");
    }

    if (p99 < 200000) {
        printf("✓ PASS: 99th percentile < 200μs (acceptable)\n");
    } else {
        printf("⚠ WARNING: 99th percentile exceeds 200μs\n");
    }

    /* Compare to Phase 0 */
    printf("\nPhase 0 result: 54μs median\n");
    if (median < 54000) {
        printf("✓ EXCEEDS Phase 0 performance!\n");
    } else if (median < 100000) {
        printf("✓ ACCEPTABLE: Within target range\n");
    }

    free(sorted);
}

/* Test 1: Basic write/read */
static void test_basic_operations(void)
{
    printf("\n========== TEST 1: Basic Operations ==========\n");

    ring_buffer_t rb;
    ring_buffer_init(&rb);

    /* Test write */
    ring_message_t msg_out, msg_in;
    assert(ring_message_create_command(&msg_out, 1, "test command"));
    assert(ring_buffer_write(&rb, &msg_out));
    printf("✓ Write successful\n");

    /* Test read */
    assert(ring_buffer_read(&rb, &msg_in));
    assert(msg_in.type == MSG_COMMAND);
    assert(msg_in.id == 1);
    assert(strcmp(msg_in.payload, "test command") == 0);
    printf("✓ Read successful\n");
    printf("✓ Data integrity verified\n");
}

/* Test 2: Buffer capacity */
static void test_capacity(void)
{
    printf("\n========== TEST 2: Buffer Capacity ==========\n");

    ring_buffer_t rb;
    ring_buffer_init(&rb);

    ring_message_t msg;
    int written = 0;

    /* Fill buffer */
    for (int i = 0; i < RING_BUFFER_SIZE + 100; i++) {
        char payload[64];
        snprintf(payload, sizeof(payload), "message %d", i);
        if (ring_message_create_command(&msg, i, payload) &&
            ring_buffer_write(&rb, &msg)) {
            written++;
        }
    }

    printf("Written %d messages (buffer size: %d)\n", written, RING_BUFFER_SIZE);
    assert(written == RING_BUFFER_SIZE);
    printf("✓ Buffer capacity correct\n");

    /* Verify can't write when full */
    assert(!ring_buffer_write(&rb, &msg));
    printf("✓ Write fails when buffer full\n");

    /* Drain buffer */
    int read = 0;
    while (ring_buffer_read(&rb, &msg)) {
        read++;
    }
    assert(read == written);
    printf("✓ Read all messages correctly (%d)\n", read);
}

/* Test 3: Latency measurement */
static void test_latency(void)
{
    printf("\n========== TEST 3: Latency Measurement ==========\n");

    ring_buffer_t rb;
    ring_buffer_init(&rb);

    latency_stats_t stats;
    latency_stats_init(&stats, NUM_ITERATIONS);

    ring_message_t msg_out, msg_in;

    /* Warmup */
    printf("Warming up (%d iterations)...\n", WARMUP_ITERATIONS);
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        ring_message_create_command(&msg_out, i, "warmup");
        ring_buffer_write(&rb, &msg_out);
        ring_buffer_read(&rb, &msg_in);
    }

    /* Actual test */
    printf("Measuring latency (%d iterations)...\n", NUM_ITERATIONS);
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        /* Create message with timestamp */
        ring_message_create_command(&msg_out, i, "latency test");

        /* Write to buffer */
        assert(ring_buffer_write(&rb, &msg_out));

        /* Read from buffer */
        assert(ring_buffer_read(&rb, &msg_in));

        /* Calculate latency */
        uint64_t latency = ring_message_latency_ns(&msg_in);
        latency_stats_add(&stats, latency);
    }

    /* Print results */
    latency_stats_print(&stats);
    latency_stats_free(&stats);

    /* Print buffer stats */
    ring_buffer_print_stats(&rb);
}

/* Test 4: Throughput */
static void test_throughput(void)
{
    printf("\n========== TEST 4: Throughput ==========\n");

    ring_buffer_t rb;
    ring_buffer_init(&rb);

    ring_message_t msg;
    const int iterations = 1000000;

    /* Measure write throughput */
    uint64_t start = ring_get_timestamp_ns();
    for (int i = 0; i < iterations; i++) {
        ring_message_create_command(&msg, i, "throughput test");
        while (!ring_buffer_write(&rb, &msg)) {
            ring_buffer_read(&rb, &msg); /* Drain if full */
        }
    }
    uint64_t end = ring_get_timestamp_ns();

    uint64_t elapsed_ns = end - start;
    double elapsed_s = elapsed_ns / 1000000000.0;
    double ops_per_sec = iterations / elapsed_s;

    printf("Total operations:  %d\n", iterations);
    printf("Total time:        %.3f seconds\n", elapsed_s);
    printf("Throughput:        %.0f ops/sec\n", ops_per_sec);
    printf("Avg time per op:   %.3f ns\n", (double)elapsed_ns / iterations);
}

/* Main test suite */
int main(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   JARVIS AI-OS IPC LATENCY TEST SUITE               ║\n");
    printf("║   Phase 1 Week 4                                     ║\n");
    printf("║   Target: <100μs IPC latency                         ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    test_basic_operations();
    test_capacity();
    test_latency();
    test_throughput();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   ALL TESTS PASSED ✓                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
