/*
 * JARVIS AI-OS - Shared Memory IPC Implementation
 * Phase 3: Lock-free SPSC ring buffer over shared memory page
 *
 * Uses __atomic_load_n / __atomic_store_n with acquire/release semantics
 * for correct ordering between producer and consumer on different cores.
 */

#include "shmem_ipc.h"

/*
 * CRC-32 (Ethernet polynomial, SEC-020).
 * Covers type + seq + length + payload[0..len-1].
 */
static uint32_t shmem_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/*
 * Compute CRC-32 over the message header fields and payload.
 * The struct is packed, so type(1)+seq(2)+length(2)+payload[] is contiguous.
 * CRC covers: 5 header bytes + payload[0..len-1].
 */
static uint32_t shmem_msg_crc(const shmem_msg_t *msg, uint16_t len)
{
    return shmem_crc32(msg, 5 + len);
}

/*
 * Initialize shared memory ring buffer.
 * Sets magic, version, size, zeroes indices and all slots.
 * Returns 0 on success, -1 on failure.
 */
int shmem_ipc_init(shmem_ring_t *ring)
{
    if (!ring)
        return -1;

    memset(ring, 0, sizeof(*ring));
    ring->header.magic   = SHMEM_MAGIC;
    ring->header.version = SHMEM_VERSION;
    ring->header.size    = SHMEM_RING_SLOTS;
    /* write_idx and read_idx are already 0 from memset */

    return 0;
}

/*
 * Send a message into the ring buffer (producer side).
 * Returns 0 on success, -1 if ring is full or arguments invalid.
 */
int shmem_ipc_send(shmem_ring_t *ring, uint8_t type, uint16_t seq,
                    const void *payload, uint16_t len)
{
    if (!ring || len > SHMEM_MAX_PAYLOAD)
        return -1;
    if (len > 0 && !payload)
        return -1;

    /* Validate ring size to prevent divide-by-zero or OOB access */
    if (ring->header.size == 0 || ring->header.size > SHMEM_RING_SLOTS)
        return -1;

    uint32_t wr = __atomic_load_n(&ring->header.write_idx, __ATOMIC_RELAXED);
    uint32_t rd = __atomic_load_n(&ring->header.read_idx, __ATOMIC_ACQUIRE);

    /* Full check: producer has filled all slots that consumer hasn't read */
    if (wr - rd >= ring->header.size)
        return -1;

    /* Write to slot */
    uint32_t slot_idx = wr % ring->header.size;
    shmem_msg_t *slot = &ring->slots[slot_idx];

    slot->type   = type;
    slot->seq    = seq;
    slot->length = len;
    if (len > 0)
        memcpy(slot->payload, payload, len);

    /* CRC-32 integrity (SEC-020) */
    slot->crc = shmem_msg_crc(slot, len);

    /* Release: ensure slot writes are visible before write_idx update */
    __atomic_store_n(&ring->header.write_idx, wr + 1, __ATOMIC_RELEASE);

    return 0;
}

/*
 * Receive a message from the ring buffer (consumer side).
 * Returns 0 on success, -1 if ring is empty or arguments invalid.
 */
int shmem_ipc_recv(shmem_ring_t *ring, uint8_t *type, uint16_t *seq,
                    void *payload, uint16_t *len)
{
    if (!ring || !type || !seq || !len)
        return -1;

    /* Validate ring size to prevent divide-by-zero or OOB access */
    if (ring->header.size == 0 || ring->header.size > SHMEM_RING_SLOTS)
        return -1;

    uint32_t rd = __atomic_load_n(&ring->header.read_idx, __ATOMIC_RELAXED);
    uint32_t wr = __atomic_load_n(&ring->header.write_idx, __ATOMIC_ACQUIRE);

    /* Empty check */
    if (rd == wr)
        return -1;

    /* Read from slot */
    uint32_t slot_idx = rd % ring->header.size;
    shmem_msg_t *slot = &ring->slots[slot_idx];

    /* SEC-001: Read ALL fields into locals ONCE — prevent TOCTOU race */
    uint8_t  local_type   = slot->type;
    uint16_t local_seq    = slot->seq;
    uint16_t local_length = slot->length;

    /* Validate length BEFORE any use — malicious producer could set > MAX */
    if (local_length > SHMEM_MAX_PAYLOAD)
        local_length = SHMEM_MAX_PAYLOAD;

    /* CRC-32 integrity check (SEC-020) — verify before consuming */
    uint32_t expected_crc = shmem_msg_crc(slot, local_length);
    if (slot->crc != expected_crc)
        return SHMEM_ERR_CRC;

    *type = local_type;
    *seq  = local_seq;
    *len  = local_length;
    if (local_length > 0 && payload)
        memcpy(payload, slot->payload, local_length);

    /* Release: ensure slot reads complete before read_idx update */
    __atomic_store_n(&ring->header.read_idx, rd + 1, __ATOMIC_RELEASE);

    return 0;
}

/*
 * Return number of messages pending in the ring.
 */
int shmem_ipc_pending(const shmem_ring_t *ring)
{
    if (!ring)
        return 0;

    uint32_t wr = __atomic_load_n(&ring->header.write_idx, __ATOMIC_ACQUIRE);
    uint32_t rd = __atomic_load_n(&ring->header.read_idx, __ATOMIC_ACQUIRE);

    return (int)(wr - rd);
}

/*
 * Reset ring indices to zero, keeping magic/version/size intact.
 */
void shmem_ipc_reset(shmem_ring_t *ring)
{
    if (!ring)
        return;

    __atomic_store_n(&ring->header.write_idx, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&ring->header.read_idx, 0, __ATOMIC_RELEASE);
}
