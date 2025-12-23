/**
 * JARVIS AI-OS Phase 1 - Driver IPC Protocol
 * Week 23: VirtIO Block Driver Implementation
 *
 * This module defines the IPC protocol for driver communication.
 * Extends existing ring buffer IPC (Week 1-4) with driver-specific message types.
 *
 * Architecture:
 * - Reuses existing ring buffer IPC (54μs latency validated)
 * - New message types for block device operations
 * - Request/response matching via request_id
 * - 5-second timeout per operation
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#ifndef DRIVER_IPC_H
#define DRIVER_IPC_H

#include <stdint.h>

/* Driver-specific message types (extend existing IPC message types) */
typedef enum {
    /* Existing message types (from ring_buffer.h): */
    /* MESSAGE_QUERY = 0, MESSAGE_RESPONSE = 1, MESSAGE_COMMAND = 2, */
    /* MESSAGE_EVENT = 3, MESSAGE_CONTROL = 4 */

    MSG_BLOCK_READ = 10,       /* Read blocks from device */
    MSG_BLOCK_WRITE = 11,      /* Write blocks to device */
    MSG_BLOCK_RESPONSE = 12,   /* Operation response (success/failure) */
    MSG_BLOCK_ERROR = 13,      /* Operation error */
    MSG_DRIVER_STATUS = 14,    /* Query driver status */
    MSG_DRIVER_STATS = 15      /* Query driver statistics */
} driver_msg_type_t;

/* Block operation types */
typedef enum {
    BLOCK_OP_READ = 0,
    BLOCK_OP_WRITE = 1,
    BLOCK_OP_FLUSH = 2
} block_op_t;

/* Block operation status */
typedef enum {
    BLOCK_STATUS_OK = 0,       /* Operation successful */
    BLOCK_STATUS_ERROR = 1,    /* Operation failed */
    BLOCK_STATUS_TIMEOUT = 2,  /* Operation timed out */
    BLOCK_STATUS_INVALID = 3   /* Invalid request */
} block_status_t;

/**
 * Block device request message.
 * Total size: 256 bytes (fits in existing IPC ring buffer message)
 */
typedef struct {
    uint32_t device_id;        /* Device identifier (0 = first device, etc.) */
    uint64_t lba;              /* Logical block address (512-byte sectors) */
    uint32_t block_count;      /* Number of blocks to read/write */
    uint32_t request_id;       /* For matching responses (unique per request) */
    uint8_t operation;         /* block_op_t (READ, WRITE, FLUSH) */
    uint8_t reserved[3];       /* Padding for alignment */
    uint8_t data[224];         /* Payload data (single block or metadata) */
} __attribute__((packed)) block_request_t;

/**
 * Block device response message.
 * Total size: 256 bytes (fits in existing IPC ring buffer message)
 */
typedef struct {
    uint32_t request_id;       /* Matching request_id from request */
    uint32_t bytes_transferred; /* Number of bytes read/written */
    uint8_t status;            /* block_status_t (OK, ERROR, TIMEOUT, INVALID) */
    uint8_t reserved[3];       /* Padding for alignment */
    uint8_t data[224];         /* Response data (single block for reads) */
    uint64_t timestamp;        /* Response timestamp (milliseconds) */
} __attribute__((packed)) block_response_t;

/**
 * Driver status message.
 * Used for querying driver state and basic information.
 */
typedef struct {
    char driver_name[32];      /* Driver name (e.g., "virtio-blk") */
    uint8_t state;             /* driver_state_t from driver_registry.h */
    uint8_t reserved[3];       /* Padding */
    uint32_t device_count;     /* Number of devices managed by driver */
    uint64_t uptime_ms;        /* Driver uptime in milliseconds */
} __attribute__((packed)) driver_status_t;

/**
 * Driver statistics message.
 * Returned in response to MSG_DRIVER_STATS.
 */
typedef struct {
    char driver_name[32];      /* Driver name */
    uint64_t total_requests;   /* Total operations requested */
    uint64_t successful_requests; /* Successfully completed operations */
    uint64_t failed_requests;  /* Failed operations */
    uint64_t bytes_read;       /* Total bytes read */
    uint64_t bytes_written;    /* Total bytes written */
    uint64_t errors;           /* Total error count */
    double avg_latency_ms;     /* Average operation latency */
} __attribute__((packed)) driver_stats_msg_t;

/* Compile-time assertions to ensure struct sizes */
_Static_assert(sizeof(block_request_t) <= 256, "block_request_t too large for IPC");
_Static_assert(sizeof(block_response_t) <= 256, "block_response_t too large for IPC");

/**
 * Helper: Create a block read request.
 *
 * Args:
 *   req: Output buffer for request
 *   device_id: Device identifier
 *   lba: Logical block address
 *   count: Number of blocks
 *   request_id: Unique request identifier
 */
static inline void create_block_read_request(block_request_t* req,
                                              uint32_t device_id,
                                              uint64_t lba,
                                              uint32_t count,
                                              uint32_t request_id) {
    req->device_id = device_id;
    req->lba = lba;
    req->block_count = count;
    req->request_id = request_id;
    req->operation = BLOCK_OP_READ;
    req->reserved[0] = req->reserved[1] = req->reserved[2] = 0;
}

/**
 * Helper: Create a block write request.
 *
 * Args:
 *   req: Output buffer for request
 *   device_id: Device identifier
 *   lba: Logical block address
 *   count: Number of blocks
 *   request_id: Unique request identifier
 *   data: Block data to write (512 bytes for single block)
 *   data_size: Size of data (max 224 bytes for single-block in message)
 */
static inline void create_block_write_request(block_request_t* req,
                                               uint32_t device_id,
                                               uint64_t lba,
                                               uint32_t count,
                                               uint32_t request_id,
                                               const void* data,
                                               uint32_t data_size) {
    req->device_id = device_id;
    req->lba = lba;
    req->block_count = count;
    req->request_id = request_id;
    req->operation = BLOCK_OP_WRITE;
    req->reserved[0] = req->reserved[1] = req->reserved[2] = 0;

    /* Copy data (max 224 bytes) */
    if (data && data_size > 0) {
        uint32_t copy_size = data_size < 224 ? data_size : 224;
        memcpy(req->data, data, copy_size);
    }
}

/**
 * Helper: Create a block response (success).
 *
 * Args:
 *   resp: Output buffer for response
 *   request_id: Matching request_id
 *   bytes_transferred: Number of bytes read/written
 *   data: Response data (for reads, 512 bytes max)
 *   data_size: Size of response data (max 224 bytes in message)
 */
static inline void create_block_response_ok(block_response_t* resp,
                                             uint32_t request_id,
                                             uint32_t bytes_transferred,
                                             const void* data,
                                             uint32_t data_size) {
    resp->request_id = request_id;
    resp->bytes_transferred = bytes_transferred;
    resp->status = BLOCK_STATUS_OK;
    resp->reserved[0] = resp->reserved[1] = resp->reserved[2] = 0;

    /* Copy data (max 224 bytes) */
    if (data && data_size > 0) {
        uint32_t copy_size = data_size < 224 ? data_size : 224;
        memcpy(resp->data, data, copy_size);
    }

    /* Set timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    resp->timestamp = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/**
 * Helper: Create a block response (error).
 *
 * Args:
 *   resp: Output buffer for response
 *   request_id: Matching request_id
 *   status: Error status (ERROR, TIMEOUT, or INVALID)
 */
static inline void create_block_response_error(block_response_t* resp,
                                                uint32_t request_id,
                                                block_status_t status) {
    resp->request_id = request_id;
    resp->bytes_transferred = 0;
    resp->status = status;
    resp->reserved[0] = resp->reserved[1] = resp->reserved[2] = 0;

    /* Set timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    resp->timestamp = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

#endif /* DRIVER_IPC_H */
