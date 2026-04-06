/*
 * JARVIS AI-OS - Shared Memory IPC Test Suite
 * Phase 3 Pre-Work: 10 tests covering init, send/recv, wrap-around,
 * all message types, edge cases, and multi-process stress.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include "shmem_ipc.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)
#define FAIL(name, reason) do { printf("  FAIL: %s -- %s\n", name, reason); tests_failed++; } while(0)

/* ---------- Test 1: Init ---------- */
static void test_init(void)
{
    const char *name = "Test 1: Init";
    shmem_ring_t ring;

    if (shmem_ipc_init(&ring) != 0) { FAIL(name, "init returned -1"); return; }
    if (ring.header.magic != SHMEM_MAGIC) { FAIL(name, "bad magic"); return; }
    if (ring.header.version != SHMEM_VERSION) { FAIL(name, "bad version"); return; }
    if (ring.header.size != SHMEM_RING_SLOTS) { FAIL(name, "bad size"); return; }
    if (ring.header.write_idx != 0) { FAIL(name, "write_idx not 0"); return; }
    if (ring.header.read_idx != 0) { FAIL(name, "read_idx not 0"); return; }

    PASS(name);
}

/* ---------- Test 2: Send single message ---------- */
static void test_send_single(void)
{
    const char *name = "Test 2: Send single message";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    const char *payload = "hello";
    if (shmem_ipc_send(&ring, MSG_QUERY, 1, payload, 5) != 0) {
        FAIL(name, "send failed"); return;
    }

    /* Verify slot content directly */
    shmem_msg_t *slot = &ring.slots[0];
    if (slot->type != MSG_QUERY) { FAIL(name, "type mismatch"); return; }
    if (slot->seq != 1) { FAIL(name, "seq mismatch"); return; }
    if (slot->length != 5) { FAIL(name, "length mismatch"); return; }
    if (memcmp(slot->payload, "hello", 5) != 0) { FAIL(name, "payload mismatch"); return; }
    if (ring.header.write_idx != 1) { FAIL(name, "write_idx not 1"); return; }

    PASS(name);
}

/* ---------- Test 3: Recv single message ---------- */
static void test_recv_single(void)
{
    const char *name = "Test 3: Recv single message";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    const char *payload = "world";
    shmem_ipc_send(&ring, MSG_RESPONSE, 42, payload, 5);

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
        FAIL(name, "recv failed"); return;
    }
    if (type != MSG_RESPONSE) { FAIL(name, "type mismatch"); return; }
    if (seq != 42) { FAIL(name, "seq mismatch"); return; }
    if (len != 5) { FAIL(name, "length mismatch"); return; }
    if (memcmp(buf, "world", 5) != 0) { FAIL(name, "payload mismatch"); return; }
    if (ring.header.read_idx != 1) { FAIL(name, "read_idx not 1"); return; }

    PASS(name);
}

/* ---------- Test 4: Fill ring (SHMEM_RING_SLOTS messages), next returns -1 ---------- */
static void test_fill_ring(void)
{
    const char *name = "Test 4: Fill ring (SHMEM_RING_SLOTS messages)";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    for (int i = 0; i < SHMEM_RING_SLOTS; i++) {
        if (shmem_ipc_send(&ring, MSG_HEARTBEAT, (uint16_t)i, "x", 1) != 0) {
            FAIL(name, "send failed before full"); return;
        }
    }

    /* Next should fail (ring full) */
    if (shmem_ipc_send(&ring, MSG_HEARTBEAT, 99, "x", 1) != -1) {
        FAIL(name, "send to full ring should return -1"); return;
    }

    if (shmem_ipc_pending(&ring) != SHMEM_RING_SLOTS) {
        FAIL(name, "pending count wrong"); return;
    }

    PASS(name);
}

/* ---------- Test 5: Empty ring recv returns -1 ---------- */
static void test_empty_recv(void)
{
    const char *name = "Test 5: Empty ring recv";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != -1) {
        FAIL(name, "recv on empty should return -1"); return;
    }

    PASS(name);
}

/* ---------- Test 6: Wrap-around ---------- */
static void test_wraparound(void)
{
    const char *name = "Test 6: Wrap-around";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    /* First pass: fill and drain 16 */
    for (int i = 0; i < SHMEM_RING_SLOTS; i++) {
        uint8_t val = (uint8_t)i;
        if (shmem_ipc_send(&ring, MSG_COMMAND, (uint16_t)i, &val, 1) != 0) {
            FAIL(name, "first pass send failed"); return;
        }
    }
    for (int i = 0; i < SHMEM_RING_SLOTS; i++) {
        if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
            FAIL(name, "first pass recv failed"); return;
        }
        if (seq != (uint16_t)i || buf[0] != (uint8_t)i) {
            FAIL(name, "first pass data mismatch"); return;
        }
    }

    /* Indices should both be SHMEM_RING_SLOTS now */
    if (ring.header.write_idx != SHMEM_RING_SLOTS || ring.header.read_idx != SHMEM_RING_SLOTS) {
        FAIL(name, "indices not SHMEM_RING_SLOTS after first pass"); return;
    }

    /* Second pass: fill and drain another SHMEM_RING_SLOTS (wraps slot index) */
    for (int i = 0; i < SHMEM_RING_SLOTS; i++) {
        uint8_t val = (uint8_t)(i + 100);
        if (shmem_ipc_send(&ring, MSG_COMMAND_RESULT, (uint16_t)(i + 100), &val, 1) != 0) {
            FAIL(name, "second pass send failed"); return;
        }
    }
    for (int i = 0; i < SHMEM_RING_SLOTS; i++) {
        if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
            FAIL(name, "second pass recv failed"); return;
        }
        if (type != MSG_COMMAND_RESULT) { FAIL(name, "second pass type mismatch"); return; }
        if (seq != (uint16_t)(i + 100)) { FAIL(name, "second pass seq mismatch"); return; }
        if (buf[0] != (uint8_t)(i + 100)) { FAIL(name, "second pass payload mismatch"); return; }
    }

    PASS(name);
}

/* ---------- Test 7: All 14 message types ---------- */
static void test_all_types(void)
{
    const char *name = "Test 7: All message types";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    uint8_t types[] = {
        MSG_QUERY, MSG_RESPONSE, MSG_HEARTBEAT, MSG_HEARTBEAT_ACK,
        MSG_STATS_REQUEST, MSG_STATS_RESPONSE, MSG_COMMAND, MSG_COMMAND_RESULT,
        MSG_SHIELD_CHECK, MSG_SHIELD_RESULT, MSG_ERROR, MSG_RESET,
        MSG_STATE_CHANGE, MSG_STATE_ACK
    };
    int count = (int)(sizeof(types) / sizeof(types[0]));

    for (int i = 0; i < count; i++) {
        char tag[4];
        snprintf(tag, sizeof(tag), "%02X", types[i]);
        if (shmem_ipc_send(&ring, types[i], (uint16_t)i, tag, 2) != 0) {
            FAIL(name, "send failed"); return;
        }
    }

    for (int i = 0; i < count; i++) {
        uint8_t type;
        uint16_t seq, len;
        char buf[SHMEM_MAX_PAYLOAD];
        if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
            FAIL(name, "recv failed"); return;
        }
        if (type != types[i]) { FAIL(name, "type mismatch"); return; }
        if (seq != (uint16_t)i) { FAIL(name, "seq mismatch"); return; }
        char expected[4];
        snprintf(expected, sizeof(expected), "%02X", types[i]);
        if (len != 2 || memcmp(buf, expected, 2) != 0) {
            FAIL(name, "payload mismatch"); return;
        }
    }

    PASS(name);
}

/* ---------- Test 8: Max payload (240 bytes) ---------- */
static void test_max_payload(void)
{
    const char *name = "Test 8: Max payload (240 bytes)";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    uint8_t data[SHMEM_MAX_PAYLOAD];
    for (int i = 0; i < SHMEM_MAX_PAYLOAD; i++)
        data[i] = (uint8_t)(i & 0xFF);

    if (shmem_ipc_send(&ring, MSG_QUERY, 999, data, SHMEM_MAX_PAYLOAD) != 0) {
        FAIL(name, "send failed"); return;
    }

    uint8_t type;
    uint16_t seq, len;
    uint8_t buf[SHMEM_MAX_PAYLOAD];
    memset(buf, 0, sizeof(buf));

    if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
        FAIL(name, "recv failed"); return;
    }
    if (len != SHMEM_MAX_PAYLOAD) { FAIL(name, "length mismatch"); return; }
    if (memcmp(buf, data, SHMEM_MAX_PAYLOAD) != 0) { FAIL(name, "payload mismatch"); return; }

    PASS(name);
}

/* ---------- Test 9: Zero payload ---------- */
static void test_zero_payload(void)
{
    const char *name = "Test 9: Zero payload";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    if (shmem_ipc_send(&ring, MSG_HEARTBEAT, 7, NULL, 0) != 0) {
        FAIL(name, "send failed"); return;
    }

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];
    memset(buf, 0xAA, sizeof(buf));

    if (shmem_ipc_recv(&ring, &type, &seq, buf, &len) != 0) {
        FAIL(name, "recv failed"); return;
    }
    if (type != MSG_HEARTBEAT) { FAIL(name, "type mismatch"); return; }
    if (seq != 7) { FAIL(name, "seq mismatch"); return; }
    if (len != 0) { FAIL(name, "length not 0"); return; }

    PASS(name);
}

/* ---------- Test 10: Multi-process stress (10,000 messages) ---------- */
static void test_multiprocess_stress(void)
{
    const char *name = "Test 10: Multi-process stress (10,000 msgs)";
    const int NUM_MSGS = 10000;

    /* Allocate shared memory ring using mmap MAP_SHARED|MAP_ANONYMOUS */
    shmem_ring_t *ring = (shmem_ring_t *)mmap(
        NULL, sizeof(shmem_ring_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (ring == MAP_FAILED) {
        FAIL(name, "mmap failed"); return;
    }

    shmem_ipc_init(ring);

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    pid_t pid = fork();
    if (pid < 0) {
        FAIL(name, "fork failed");
        munmap(ring, sizeof(shmem_ring_t));
        return;
    }

    if (pid == 0) {
        /* Child: consumer -- receive NUM_MSGS messages */
        int received = 0;
        int corrupted = 0;

        while (received < NUM_MSGS) {
            uint8_t type;
            uint16_t seq, len;
            uint8_t buf[SHMEM_MAX_PAYLOAD];

            if (shmem_ipc_recv(ring, &type, &seq, buf, &len) == 0) {
                /* Verify: seq should match received count, payload[0] = seq & 0xFF */
                if (seq != (uint16_t)(received & 0xFFFF)) corrupted++;
                if (len != 4) corrupted++;
                else {
                    uint32_t expected_val = (uint32_t)received;
                    uint32_t actual_val;
                    memcpy(&actual_val, buf, 4);
                    if (actual_val != expected_val) corrupted++;
                }
                received++;
            }
            /* Spin if empty -- SPSC producer will catch up */
        }

        /* Exit with corruption count (0 = success) */
        _exit(corrupted > 127 ? 127 : corrupted);
    } else {
        /* Parent: producer -- send NUM_MSGS messages */
        int sent = 0;
        while (sent < NUM_MSGS) {
            uint32_t val = (uint32_t)sent;
            if (shmem_ipc_send(ring, MSG_QUERY, (uint16_t)(sent & 0xFFFF),
                               &val, 4) == 0) {
                sent++;
            }
            /* Spin if full -- consumer will drain */
        }

        /* Wait for child */
        int status;
        waitpid(pid, &status, 0);

        clock_gettime(CLOCK_MONOTONIC, &t_end);

        double elapsed = (t_end.tv_sec - t_start.tv_sec)
                       + (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
        double throughput = NUM_MSGS / elapsed;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("         %d messages, %.3f sec, %.0f msg/sec\n",
                   NUM_MSGS, elapsed, throughput);
            PASS(name);
        } else {
            char reason[128];
            snprintf(reason, sizeof(reason),
                     "child exit %d (corrupted messages)",
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            FAIL(name, reason);
        }
    }

    munmap(ring, sizeof(shmem_ring_t));
}

/* ---------- Main ---------- */
int main(void)
{
    printf("\n========== JARVIS Phase 3: Shared Memory IPC Test Suite ==========\n\n");

    /* Compile-time layout check */
    if (sizeof(shmem_ring_header_t) != 64) {
        printf("  FATAL: shmem_ring_header_t is %zu bytes, expected 64\n",
               sizeof(shmem_ring_header_t));
        return 1;
    }
    if (sizeof(shmem_msg_t) != SHMEM_SLOT_SIZE) {
        printf("  FATAL: shmem_msg_t is %zu bytes, expected %d\n",
               sizeof(shmem_msg_t), SHMEM_SLOT_SIZE);
        return 1;
    }
    if (sizeof(shmem_ring_t) > SHMEM_PAGE_SIZE) {
        printf("  WARNING: shmem_ring_t is %zu bytes (exceeds page size %d)\n",
               sizeof(shmem_ring_t), SHMEM_PAGE_SIZE);
    }

    test_init();
    test_send_single();
    test_recv_single();
    test_fill_ring();
    test_empty_recv();
    test_wraparound();
    test_all_types();
    test_max_payload();
    test_zero_payload();
    test_multiprocess_stress();

    printf("\n========== Results: %d PASS, %d FAIL ==========\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
