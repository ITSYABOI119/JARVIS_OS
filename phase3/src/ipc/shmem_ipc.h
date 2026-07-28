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
#define SHMEM_RING_SLOTS   15   /* Must fit in 4KB page: 64 + 15*256 = 3904 < 4096 */
#define SHMEM_SLOT_SIZE    256
#define SHMEM_MAX_PAYLOAD  240
#define SHMEM_PAGE_SIZE    4096

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
#define MSG_DEBUG          0x0F  /* Debug log from Process B -> Process A for NVMe logging */
/* 0x10 reserved (was MSG_MODEL_SWAP, removed 2026-04-17) */
/* 0x12 reserved for MSG_EMBED (Phase C / C/M1b design §4.5) — do NOT take it. */
#define MSG_QUERY_LONG     0x13  /* PA -> PB: a query whose lane may generate a LONG answer.
                                  * Identical to MSG_QUERY except for the generation token cap.
                                  * The lane is not otherwise visible to PB — PA sends the same
                                  * MSG_QUERY for the synthetic workload, the wake lane and the
                                  * probes, so the CAP has to ride the message. Only pa_ctrl_gate
                                  * (the lane a human actually reads) sends this. */
#define MSG_INFER_STATS    0x11  /* PB -> PA: per-inference tokens + TSC cycles (v4 live tok/s).
                                  * Sent BEFORE the MSG_RESPONSE chunks so PA latches it in the
                                  * same drain pass (no terminator race). Always-on, tiny. */

/* WHY GENERATION STOPPED. Three outcomes that all look like "a short answer" from outside, and
 * only two of them mean the model was cut off mid-thought:
 *   CAP     — hit the lane's token budget with more to say  => TRUNCATED
 *   KV_FULL — ran out of KV context with more to say        => TRUNCATED
 *   ENDED   — the model emitted its declared end-of-turn    => COMPLETE, not truncation
 * Marking ENDED as truncated would cry wolf on every short factual answer, which is most of them.
 * UNKNOWN is the fail-safe: a stats message that never arrived, or one from a PB too old to carry
 * the field, reports UNKNOWN and PA treats it as NOT truncated — a missing signal must never
 * manufacture a truncation claim. */
#define PB_STOP_UNKNOWN     0
#define PB_STOP_MODEL_ENDED 1
#define PB_STOP_CAP         2
#define PB_STOP_KV_FULL     3

/* MSG_INFER_STATS payload — a real per-inference measurement (never a benchmark constant).
 * PA divides by its TSC_PER_MS to derive ms -> tok/s; PB reports RAW tokens + cycles.
 *
 * #9 appended `stop_reason`. THIS IS AN INTERNAL IPC STRUCT, NOT A WIRE FORMAT — PA and PB are
 * built from the same tree and ship together, so there is no version to bump and no lockstep
 * beyond this file. PA still size-checks defensively rather than assuming (infer_stats_latch),
 * because "they always match" is an assumption that costs nothing to stop making. */
typedef struct __attribute__((packed)) {
    uint32_t tokens;      /* tokens generated this inference (n_gen) */
    uint64_t tsc_cycles;  /* RDTSC delta over the generation loop */
    uint8_t  stop_reason; /* PB_STOP_* — #9: was the answer cut off, or did it finish? */
} infer_stats_msg_t;

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

_Static_assert(sizeof(shmem_ring_t) <= 4096,
    "shmem_ring_t must fit in a single 4KB page");

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
