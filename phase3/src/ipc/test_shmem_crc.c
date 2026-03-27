/*
 * JARVIS AI-OS - Shared Memory IPC CRC-32 Test Suite
 * Phase 3: Tests for per-message CRC-32 integrity verification (SEC-020)
 *
 * 3 tests:
 *   1. Normal send+recv — CRC valid, recv succeeds
 *   2. Corrupt payload byte — recv returns SHMEM_ERR_CRC
 *   3. Empty payload (len=0) — CRC still computed correctly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shmem_ipc.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)
#define FAIL(name, reason) do { printf("  FAIL: %s -- %s\n", name, reason); tests_failed++; } while(0)

/* ---------- Test 1: Normal send+recv with valid CRC ---------- */
static void test_crc_valid(void)
{
    const char *name = "Test 1: CRC valid on normal send+recv";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    const char *payload = "CRC integrity test";
    uint16_t plen = (uint16_t)strlen(payload);

    if (shmem_ipc_send(&ring, MSG_QUERY, 100, payload, plen) != 0) {
        FAIL(name, "send failed"); return;
    }

    /* Verify CRC field was set (non-zero for non-trivial data) */
    shmem_msg_t *slot = &ring.slots[0];
    if (slot->crc == 0) {
        FAIL(name, "CRC field is zero after send"); return;
    }

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    int ret = shmem_ipc_recv(&ring, &type, &seq, buf, &len);
    if (ret != 0) {
        FAIL(name, "recv failed (CRC should be valid)"); return;
    }
    if (type != MSG_QUERY || seq != 100 || len != plen) {
        FAIL(name, "header mismatch"); return;
    }
    if (memcmp(buf, payload, plen) != 0) {
        FAIL(name, "payload mismatch"); return;
    }

    PASS(name);
}

/* ---------- Test 2: Corrupt payload byte — recv returns CRC error ---------- */
static void test_crc_corrupt(void)
{
    const char *name = "Test 2: CRC detects corrupted payload";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    const char *payload = "hello world";
    uint16_t plen = (uint16_t)strlen(payload);

    if (shmem_ipc_send(&ring, MSG_RESPONSE, 42, payload, plen) != 0) {
        FAIL(name, "send failed"); return;
    }

    /* Corrupt one byte of payload directly in the slot (simulating bit flip) */
    shmem_msg_t *slot = &ring.slots[0];
    slot->payload[3] ^= 0xFF;  /* Flip all bits of payload[3] */

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    int ret = shmem_ipc_recv(&ring, &type, &seq, buf, &len);
    if (ret != SHMEM_ERR_CRC) {
        char reason[128];
        snprintf(reason, sizeof(reason),
                 "expected SHMEM_ERR_CRC (%d), got %d", SHMEM_ERR_CRC, ret);
        FAIL(name, reason); return;
    }

    /* Verify the message was NOT consumed (read_idx still 0) */
    if (ring.header.read_idx != 0) {
        FAIL(name, "read_idx advanced despite CRC failure"); return;
    }

    PASS(name);
}

/* ---------- Test 3: Empty payload (len=0) — CRC still works ---------- */
static void test_crc_empty_payload(void)
{
    const char *name = "Test 3: CRC valid with empty payload";
    shmem_ring_t ring;
    shmem_ipc_init(&ring);

    if (shmem_ipc_send(&ring, MSG_HEARTBEAT, 7, NULL, 0) != 0) {
        FAIL(name, "send failed"); return;
    }

    /* CRC should still be set (covers type+seq+length = 5 bytes) */
    shmem_msg_t *slot = &ring.slots[0];
    if (slot->crc == 0) {
        FAIL(name, "CRC field is zero for empty payload"); return;
    }

    uint8_t type;
    uint16_t seq, len;
    char buf[SHMEM_MAX_PAYLOAD];

    int ret = shmem_ipc_recv(&ring, &type, &seq, buf, &len);
    if (ret != 0) {
        FAIL(name, "recv failed (CRC should be valid for empty payload)"); return;
    }
    if (type != MSG_HEARTBEAT || seq != 7 || len != 0) {
        FAIL(name, "header mismatch"); return;
    }

    PASS(name);
}

/* ---------- Main ---------- */
int main(void)
{
    printf("\n========== JARVIS Phase 3: Shared Memory IPC CRC-32 Test Suite ==========\n\n");

    /* Compile-time layout check */
    if (sizeof(shmem_msg_t) != SHMEM_SLOT_SIZE) {
        printf("  FATAL: shmem_msg_t is %zu bytes, expected %d\n",
               sizeof(shmem_msg_t), SHMEM_SLOT_SIZE);
        return 1;
    }

    test_crc_valid();
    test_crc_corrupt();
    test_crc_empty_payload();

    printf("\n========== Results: %d PASS, %d FAIL ==========\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
