/**
 * JARVIS AI-OS Phase 1 - Driver Registry
 * Week 23: VirtIO Block Driver Implementation
 *
 * This module provides a central registry for managing device drivers in user-space.
 * All drivers register here and are managed through a state machine.
 *
 * Architecture:
 * - User-space drivers (microkernel-compatible)
 * - State machine: UNLOADED -> LOADED -> ACTIVE -> SUSPENDED -> FAILED
 * - Thread-safe operation (mutex locks)
 * - Statistics tracking per driver
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* Driver state enumeration */
typedef enum {
    DRIVER_UNLOADED = 0,    /* Driver not yet loaded */
    DRIVER_LOADED = 1,      /* Driver loaded but not active */
    DRIVER_ACTIVE = 2,      /* Driver running and handling requests */
    DRIVER_SUSPENDED = 3,   /* Driver suspended (Week 22 integration) */
    DRIVER_FAILED = 4       /* Driver encountered fatal error */
} driver_state_t;

/* Driver statistics */
typedef struct {
    uint64_t total_requests;      /* Total operations requested */
    uint64_t successful_requests; /* Successfully completed operations */
    uint64_t failed_requests;     /* Failed operations */
    uint64_t bytes_read;          /* Total bytes read */
    uint64_t bytes_written;       /* Total bytes written */
    uint64_t errors;              /* Total error count */
    double avg_latency_ms;        /* Average operation latency */
} driver_stats_t;

/* Forward declaration */
struct driver_info;

/* Driver operations interface */
typedef struct {
    int (*init)(void* private_data);                              /* Initialize driver */
    int (*start)(void* private_data);                             /* Start driver operations */
    int (*stop)(void* private_data);                              /* Stop driver operations */
    int (*suspend)(void* private_data);                           /* Suspend driver (Week 22) */
    int (*resume)(void* private_data);                            /* Resume driver (Week 22) */
    int (*read)(void* private_data, uint64_t lba, uint32_t count, void* buffer);   /* Read blocks */
    int (*write)(void* private_data, uint64_t lba, uint32_t count, const void* buffer); /* Write blocks */
    void (*cleanup)(void* private_data);                          /* Cleanup driver resources */
} driver_ops_t;

/* Driver information structure */
typedef struct driver_info {
    char name[32];                /* Driver name (e.g., "virtio-blk") */
    driver_state_t state;         /* Current state */
    void* private_data;           /* Driver-specific private data */
    driver_ops_t* ops;            /* Driver operation functions */
    driver_stats_t stats;         /* Driver statistics */
    pthread_mutex_t lock;         /* Thread-safety lock */
    uint64_t last_activity_time;  /* Timestamp of last operation (monotonic) */
} driver_info_t;

/* Driver registry structure */
typedef struct {
    driver_info_t drivers[32];    /* Array of registered drivers (max 32) */
    uint32_t driver_count;        /* Number of registered drivers */
    pthread_mutex_t registry_lock; /* Registry-wide lock */
} driver_registry_t;

/**
 * Initialize the driver registry.
 * Must be called once at system startup.
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int driver_registry_init(void);

/**
 * Register a new driver with the registry.
 *
 * Args:
 *   name: Driver name (max 31 characters)
 *   ops: Driver operations structure
 *   private_data: Driver-specific private data (can be NULL)
 *
 * Returns:
 *   0 on success, -1 on failure (registry full or duplicate name)
 */
int driver_register(const char* name, driver_ops_t* ops, void* private_data);

/**
 * Unregister a driver from the registry.
 * Driver must be in LOADED state (not ACTIVE).
 *
 * Args:
 *   name: Driver name
 *
 * Returns:
 *   0 on success, -1 on failure (driver not found or still active)
 */
int driver_unregister(const char* name);

/**
 * Start a driver (transition LOADED -> ACTIVE).
 *
 * Args:
 *   name: Driver name
 *
 * Returns:
 *   0 on success, -1 on failure (invalid state or driver error)
 */
int driver_start(const char* name);

/**
 * Stop a driver (transition ACTIVE -> LOADED).
 *
 * Args:
 *   name: Driver name
 *
 * Returns:
 *   0 on success, -1 on failure (invalid state or driver error)
 */
int driver_stop(const char* name);

/**
 * Suspend a driver (transition ACTIVE -> SUSPENDED).
 * Week 22 integration - for system suspend/resume.
 *
 * Args:
 *   name: Driver name
 *
 * Returns:
 *   0 on success, -1 on failure (invalid state or driver error)
 */
int driver_suspend(const char* name);

/**
 * Resume a driver (transition SUSPENDED -> ACTIVE).
 * Week 22 integration - for system suspend/resume.
 *
 * Args:
 *   name: Driver name
 *
 * Returns:
 *   0 on success, -1 on failure (invalid state or driver error)
 */
int driver_resume(const char* name);

/**
 * Get driver information and statistics.
 *
 * Args:
 *   name: Driver name
 *   info: Output buffer for driver info
 *
 * Returns:
 *   0 on success, -1 on failure (driver not found)
 */
int driver_get_info(const char* name, driver_info_t* info);

/**
 * Get driver statistics.
 *
 * Args:
 *   name: Driver name
 *   stats: Output buffer for statistics
 *
 * Returns:
 *   0 on success, -1 on failure (driver not found)
 */
int driver_get_stats(const char* name, driver_stats_t* stats);

/**
 * List all registered drivers.
 *
 * Args:
 *   names: Output buffer for driver names (array of char[32])
 *   max_count: Maximum number of names to return
 *   count: Output - actual number of drivers returned
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int driver_list(char names[][32], uint32_t max_count, uint32_t* count);

/**
 * Perform a read operation via driver.
 * Driver must be in ACTIVE state.
 *
 * Args:
 *   name: Driver name
 *   lba: Logical block address (512-byte sectors)
 *   count: Number of blocks to read
 *   buffer: Output buffer (must be at least count * 512 bytes)
 *
 * Returns:
 *   Number of bytes read on success, -1 on failure
 */
int driver_read(const char* name, uint64_t lba, uint32_t count, void* buffer);

/**
 * Perform a write operation via driver.
 * Driver must be in ACTIVE state.
 *
 * Args:
 *   name: Driver name
 *   lba: Logical block address (512-byte sectors)
 *   count: Number of blocks to write
 *   buffer: Input buffer (must be at least count * 512 bytes)
 *
 * Returns:
 *   Number of bytes written on success, -1 on failure
 */
int driver_write(const char* name, uint64_t lba, uint32_t count, const void* buffer);

/**
 * Cleanup and shutdown the driver registry.
 * Stops and unregisters all drivers.
 * Should be called at system shutdown.
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int driver_registry_cleanup(void);

/**
 * Helper: Get string representation of driver state.
 *
 * Args:
 *   state: Driver state
 *
 * Returns:
 *   String representation (e.g., "ACTIVE")
 */
const char* driver_state_to_string(driver_state_t state);

#endif /* DRIVER_REGISTRY_H */
