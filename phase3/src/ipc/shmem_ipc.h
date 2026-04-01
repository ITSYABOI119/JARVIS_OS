/*
 * JARVIS AI-OS - Shared Memory IPC for Phase 3
 * Replaces UART framing with lock-free ring buffer over shared page
 *
 * Uses atomic load/store with acquire/release for lock-free SPSC operation.
 * Per-message CRC-32 integrity verification protects against shared memory
 * corruption (bit flips, partial writes). See SEC-020.
 */

#ifndef SHMEM_IPC_H
#define SHMEM_IPC_H

#include <stdint.h>
#include <string.h>

#define SHMEM_MAGIC        0xDEADBEEF
#define SHMEM_VERSION      1
#define SHMEM_RING_SLOTS   16
#define SHMEM_SLOT_SIZE    256
#define SHMEM_MAX_PAYLOAD  240
#define SHMEM_PAGE_SIZE    4096  /* One page: 64B header + 16 * 256B slots */

/* Message types (same as Phase 2 UART protocol) */
#define MSG_QUERY          0x01
#define MSG_RESPONSE       0x02
#define MSG_HEARTBEAT      0x03
#define MSG_HEARTBEAT_ACK  0x04
#define MSG_STATS_REQUEST  0x05
#define MSG_STATS_RESPONSE 0x06
#define MSG_COMMAND        0x07
#define MSG_COMMAND_RESULT 0x08
#define MSG_SHIELD_CHECK   0x09
#define MSG_SHIELD_RESULT  0x0A
#define MSG_ERROR          0x0B
#define MSG_RESET          0x0C
#define MSG_STATE_CHANGE   0x0D
#define MSG_STATE_ACK      0x0E
#define MSG_SET_TQ_MODE    0x0F  /* Enable/disable TurboQuant: payload[0]=1 enable, 0 disable */

/* Ring buffer header (64 bytes, at start of shared page) */
typedef struct {
    uint32_t magic;       /* SHMEM_MAGIC */
    uint32_t version;     /* SHMEM_VERSION */
    uint32_t write_idx;   /* Producer increments (atomic) */
    uint32_t read_idx;    /* Consumer increments (atomic) */
    uint32_t size;        /* Number of slots (SHMEM_RING_SLOTS) */
    uint32_t padding[11]; /* Pad to 64 bytes */
} shmem_ring_header_t;

/* Message slot (5-byte header + payload + CRC, padded to SHMEM_SLOT_SIZE) */
typedef struct __attribute__((packed)) {
    uint8_t  type;                        /* MSG_QUERY, MSG_RESPONSE, etc. */
    uint16_t seq;                         /* Sequence number */
    uint16_t length;                      /* Payload length (0-240) */
    uint8_t  payload[SHMEM_MAX_PAYLOAD];  /* Payload data */
    uint32_t crc;                         /* CRC-32 over type+seq+length+payload[0..len-1] */
    uint8_t  _pad[SHMEM_SLOT_SIZE - 5 - SHMEM_MAX_PAYLOAD - 4]; /* Pad to slot size */
} shmem_msg_t;

/* Full shared memory page layout */
typedef struct {
    shmem_ring_header_t header;
    shmem_msg_t slots[SHMEM_RING_SLOTS];
} shmem_ring_t;

/* Error codes */
#define SHMEM_ERR_CRC      (-2)  /* CRC-32 mismatch on recv (SEC-020) */

/* API */
int  shmem_ipc_init(shmem_ring_t *ring);
int  shmem_ipc_send(shmem_ring_t *ring, uint8_t type, uint16_t seq,
                     const void *payload, uint16_t len);
int  shmem_ipc_recv(shmem_ring_t *ring, uint8_t *type, uint16_t *seq,
                     void *payload, uint16_t *len);
int  shmem_ipc_pending(const shmem_ring_t *ring);
void shmem_ipc_reset(shmem_ring_t *ring);

#endif /* SHMEM_IPC_H */
