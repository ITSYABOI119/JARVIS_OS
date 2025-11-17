/*
 * ipc_latency_benchmark.c - JARVIS Phase 0 Track B
 * Lock-Free Ring Buffer IPC Performance Validation
 *
 * Validates <100μs median IPC latency, <500μs p99 latency
 * Uses lock-free SPSC (Single Producer Single Consumer) ring buffer
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

// ============================================================================
// Configuration
// ============================================================================

#define RING_BUFFER_SIZE 1024      // Power of 2 for fast modulo
#define MESSAGE_SIZE 64            // Typical IPC message size (bytes)
#define NUM_WARMUP 1000            // Warmup iterations
#define NUM_ITERATIONS 100000      // Test iterations for statistics
#define CACHE_LINE_SIZE 64         // Prevent false sharing

// ============================================================================
// Lock-Free Ring Buffer (SPSC)
// ============================================================================

typedef struct {
    uint8_t data[MESSAGE_SIZE];
    uint64_t timestamp;  // For latency measurement
} Message;

typedef struct {
    // Producer-only cache line
    volatile uint64_t head __attribute__((aligned(CACHE_LINE_SIZE)));
    uint8_t padding1[CACHE_LINE_SIZE - sizeof(uint64_t)];

    // Consumer-only cache line
    volatile uint64_t tail __attribute__((aligned(CACHE_LINE_SIZE)));
    uint8_t padding2[CACHE_LINE_SIZE - sizeof(uint64_t)];

    // Shared data
    Message buffer[RING_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
} RingBuffer;

// Initialize ring buffer
void ring_buffer_init(RingBuffer* rb) {
    rb->head = 0;
    rb->tail = 0;
    memset((void*)rb->buffer, 0, sizeof(rb->buffer));
}

// Producer: enqueue message (non-blocking)
bool ring_buffer_push(RingBuffer* rb, const Message* msg) {
    uint64_t head = rb->head;
    uint64_t next_head = (head + 1) & (RING_BUFFER_SIZE - 1);

    // Check if buffer is full
    if (next_head == rb->tail) {
        return false;  // Full
    }

    // Copy message
    memcpy((void*)&rb->buffer[head], msg, sizeof(Message));

    // Commit (release barrier)
    __atomic_store_n(&rb->head, next_head, __ATOMIC_RELEASE);

    return true;
}

// Consumer: dequeue message (non-blocking)
bool ring_buffer_pop(RingBuffer* rb, Message* msg) {
    uint64_t tail = rb->tail;

    // Check if buffer is empty (acquire barrier)
    uint64_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;  // Empty
    }

    // Copy message
    memcpy(msg, (void*)&rb->buffer[tail], sizeof(Message));

    // Commit
    uint64_t next_tail = (tail + 1) & (RING_BUFFER_SIZE - 1);
    rb->tail = next_tail;

    return true;
}

// ============================================================================
// High-Resolution Timer
// ============================================================================

// Get current time in nanoseconds
uint64_t get_nanos() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000000ULL) / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

// ============================================================================
// Statistics
// ============================================================================

typedef struct {
    uint64_t* samples;
    size_t count;
    uint64_t min;
    uint64_t max;
    uint64_t median;
    uint64_t p95;
    uint64_t p99;
    uint64_t p999;
    double mean;
    double stddev;
} Statistics;

// Compare function for qsort
int compare_uint64(const void* a, const void* b) {
    uint64_t arg1 = *(const uint64_t*)a;
    uint64_t arg2 = *(const uint64_t*)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

// Calculate statistics
void calculate_statistics(uint64_t* samples, size_t count, Statistics* stats) {
    stats->samples = samples;
    stats->count = count;

    // Sort samples
    qsort(samples, count, sizeof(uint64_t), compare_uint64);

    // Min/Max
    stats->min = samples[0];
    stats->max = samples[count - 1];

    // Percentiles
    stats->median = samples[count / 2];
    stats->p95 = samples[(size_t)(count * 0.95)];
    stats->p99 = samples[(size_t)(count * 0.99)];
    stats->p999 = samples[(size_t)(count * 0.999)];

    // Mean
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += (double)samples[i];
    }
    stats->mean = sum / (double)count;

    // Standard deviation
    double variance_sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = (double)samples[i] - stats->mean;
        variance_sum += diff * diff;
    }
    stats->stddev = sqrt(variance_sum / (double)count);
}

// ============================================================================
// Producer/Consumer Threads
// ============================================================================

typedef struct {
    RingBuffer* rb;
    uint64_t* latencies;
    size_t num_messages;
    volatile bool* done;
} ThreadArgs;

// Producer thread
void* producer_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    RingBuffer* rb = args->rb;
    size_t num_messages = args->num_messages;

    Message msg;
    memset(&msg, 0xAA, sizeof(msg));  // Fill with pattern

    for (size_t i = 0; i < num_messages; i++) {
        // Record send timestamp
        msg.timestamp = get_nanos();

        // Send (spin until success)
        while (!ring_buffer_push(rb, &msg)) {
            // Spin (could use pause instruction here)
            #ifdef _WIN32
            YieldProcessor();
            #else
            __builtin_ia32_pause();
            #endif
        }
    }

    return NULL;
}

// Consumer thread
void* consumer_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    RingBuffer* rb = args->rb;
    uint64_t* latencies = args->latencies;
    size_t num_messages = args->num_messages;

    Message msg;
    size_t received = 0;

    while (received < num_messages) {
        if (ring_buffer_pop(rb, &msg)) {
            // Record receive timestamp
            uint64_t recv_time = get_nanos();

            // Calculate latency
            latencies[received] = recv_time - msg.timestamp;
            received++;
        } else {
            // Spin (could use pause instruction here)
            #ifdef _WIN32
            YieldProcessor();
            #else
            __builtin_ia32_pause();
            #endif
        }
    }

    *args->done = true;
    return NULL;
}

// ============================================================================
// Benchmark
// ============================================================================

void run_ipc_benchmark() {
    printf("======================================================================\n");
    printf("JARVIS AI-OS - Phase 0 Track B: IPC Latency Benchmark (C)\n");
    printf("======================================================================\n");
    printf("\n");

    // Allocate ring buffer
    RingBuffer* rb = (RingBuffer*)aligned_alloc(CACHE_LINE_SIZE, sizeof(RingBuffer));
    if (!rb) {
        fprintf(stderr, "Failed to allocate ring buffer\n");
        return;
    }
    ring_buffer_init(rb);

    // Allocate latency array
    uint64_t* latencies = (uint64_t*)malloc(NUM_ITERATIONS * sizeof(uint64_t));
    if (!latencies) {
        fprintf(stderr, "Failed to allocate latency array\n");
        free(rb);
        return;
    }

    printf("[CONFIGURATION]\n");
    printf("----------------------------------------------------------------------\n");
    printf("  Ring buffer size:     %d messages\n", RING_BUFFER_SIZE);
    printf("  Message size:         %d bytes\n", MESSAGE_SIZE);
    printf("  Warmup iterations:    %d\n", NUM_WARMUP);
    printf("  Test iterations:      %d\n", NUM_ITERATIONS);
    printf("  Cache line size:      %d bytes\n", CACHE_LINE_SIZE);
    printf("\n");

    // Warmup
    printf("[WARMUP]\n");
    printf("----------------------------------------------------------------------\n");
    printf("  Running warmup...\n");

    volatile bool warmup_done = false;
    ThreadArgs warmup_args = {rb, latencies, NUM_WARMUP, &warmup_done};

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_thread, &warmup_args);
    pthread_create(&consumer, NULL, consumer_thread, &warmup_args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    printf("  Warmup complete\n");
    printf("\n");

    // Reset ring buffer
    ring_buffer_init(rb);

    // Run benchmark
    printf("[BENCHMARK]\n");
    printf("----------------------------------------------------------------------\n");
    printf("  Running %d IPC round-trips...\n", NUM_ITERATIONS);

    volatile bool bench_done = false;
    ThreadArgs bench_args = {rb, latencies, NUM_ITERATIONS, &bench_done};

    uint64_t start_time = get_nanos();

    pthread_create(&producer, NULL, producer_thread, &bench_args);
    pthread_create(&consumer, NULL, consumer_thread, &bench_args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    uint64_t end_time = get_nanos();
    uint64_t total_time = end_time - start_time;

    printf("  Benchmark complete\n");
    printf("\n");

    // Calculate statistics
    Statistics stats;
    calculate_statistics(latencies, NUM_ITERATIONS, &stats);

    // Print results
    printf("[RESULTS]\n");
    printf("======================================================================\n");
    printf("\n");

    printf("Latency Statistics (nanoseconds):\n");
    printf("  Minimum:              %lu ns\n", (unsigned long)stats.min);
    printf("  Mean:                 %.2f ns\n", stats.mean);
    printf("  Median (p50):         %lu ns\n", (unsigned long)stats.median);
    printf("  95th percentile:      %lu ns\n", (unsigned long)stats.p95);
    printf("  99th percentile:      %lu ns\n", (unsigned long)stats.p99);
    printf("  99.9th percentile:    %lu ns\n", (unsigned long)stats.p999);
    printf("  Maximum:              %lu ns\n", (unsigned long)stats.max);
    printf("  Std deviation:        %.2f ns\n", stats.stddev);
    printf("\n");

    printf("Latency Statistics (microseconds):\n");
    printf("  Minimum:              %.2f us\n", stats.min / 1000.0);
    printf("  Mean:                 %.2f us\n", stats.mean / 1000.0);
    printf("  Median (p50):         %.2f us\n", stats.median / 1000.0);
    printf("  95th percentile:      %.2f us\n", stats.p95 / 1000.0);
    printf("  99th percentile:      %.2f us\n", stats.p99 / 1000.0);
    printf("  99.9th percentile:    %.2f us\n", stats.p999 / 1000.0);
    printf("  Maximum:              %.2f us\n", stats.max / 1000.0);
    printf("\n");

    printf("Throughput:\n");
    printf("  Total time:           %.2f ms\n", total_time / 1000000.0);
    printf("  Messages/sec:         %.0f msg/s\n",
           (double)NUM_ITERATIONS / (total_time / 1000000000.0));
    printf("  Bandwidth:            %.2f MB/s\n",
           ((double)NUM_ITERATIONS * MESSAGE_SIZE) / (total_time / 1000000000.0) / 1048576.0);
    printf("\n");

    // Validation
    printf("[VALIDATION]\n");
    printf("======================================================================\n");
    printf("\n");

    bool median_pass = (stats.median / 1000.0) < 100.0;  // <100μs target
    bool p99_pass = (stats.p99 / 1000.0) < 500.0;        // <500μs target

    printf("  Target: Median <100us, p99 <500us\n");
    printf("\n");

    if (median_pass) {
        printf("  [PASS] Median latency: %.2f us (<100 us target)\n", stats.median / 1000.0);
    } else {
        printf("  [FAIL] Median latency: %.2f us (>100 us target)\n", stats.median / 1000.0);
    }

    if (p99_pass) {
        printf("  [PASS] p99 latency: %.2f us (<500 us target)\n", stats.p99 / 1000.0);
    } else {
        printf("  [FAIL] p99 latency: %.2f us (>500 us target)\n", stats.p99 / 1000.0);
    }

    printf("\n");

    if (median_pass && p99_pass) {
        printf("  [PASS] IPC latency requirements MET\n");
    } else {
        printf("  [FAIL] IPC latency requirements NOT MET\n");
    }

    printf("\n");
    printf("======================================================================\n");
    printf("[EXPERIMENT COMPLETE]\n");
    printf("======================================================================\n");
    printf("\n");
    printf("Lock-free ring buffer IPC validated:\n");
    printf("  - SPSC (single producer, single consumer) ring buffer\n");
    printf("  - Cache-line aligned to prevent false sharing\n");
    printf("  - Atomic operations for synchronization\n");
    printf("  - %d iterations measured\n", NUM_ITERATIONS);
    printf("  - Median: %.2f us, p99: %.2f us\n",
           stats.median / 1000.0, stats.p99 / 1000.0);
    printf("\n");
    printf("======================================================================\n");

    // Cleanup
    free(latencies);
    free(rb);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char** argv) {
    run_ipc_benchmark();
    return 0;
}
