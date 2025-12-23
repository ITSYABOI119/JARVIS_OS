/**
 * JARVIS AI-OS Phase 1 - Driver Registry Implementation
 * Week 23: VirtIO Block Driver Implementation
 *
 * Central registry for managing device drivers in user-space.
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#include "driver_registry.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

/* Global driver registry */
static driver_registry_t g_registry;
static bool g_registry_initialized = false;

/* Helper: Get monotonic timestamp in milliseconds */
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Helper: Find driver by name (must hold registry_lock) */
static driver_info_t* find_driver_unlocked(const char* name) {
    for (uint32_t i = 0; i < g_registry.driver_count; i++) {
        if (strcmp(g_registry.drivers[i].name, name) == 0) {
            return &g_registry.drivers[i];
        }
    }
    return NULL;
}

int driver_registry_init(void) {
    if (g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Already initialized\n");
        return -1;
    }

    memset(&g_registry, 0, sizeof(driver_registry_t));

    if (pthread_mutex_init(&g_registry.registry_lock, NULL) != 0) {
        fprintf(stderr, "[DriverRegistry] Failed to initialize registry lock\n");
        return -1;
    }

    g_registry_initialized = true;
    printf("[DriverRegistry] Initialized (max 32 drivers)\n");
    return 0;
}

int driver_register(const char* name, driver_ops_t* ops, void* private_data) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    if (!name || !ops) {
        fprintf(stderr, "[DriverRegistry] Invalid arguments\n");
        return -1;
    }

    if (strlen(name) >= 32) {
        fprintf(stderr, "[DriverRegistry] Driver name too long: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    /* Check if driver already exists */
    if (find_driver_unlocked(name) != NULL) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver already registered: %s\n", name);
        return -1;
    }

    /* Check if registry is full */
    if (g_registry.driver_count >= 32) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Registry full (max 32 drivers)\n");
        return -1;
    }

    /* Add new driver */
    driver_info_t* driver = &g_registry.drivers[g_registry.driver_count];
    strncpy(driver->name, name, sizeof(driver->name) - 1);
    driver->name[sizeof(driver->name) - 1] = '\0';
    driver->state = DRIVER_UNLOADED;
    driver->ops = ops;
    driver->private_data = private_data;
    memset(&driver->stats, 0, sizeof(driver_stats_t));
    pthread_mutex_init(&driver->lock, NULL);
    driver->last_activity_time = get_timestamp_ms();

    /* Call driver init */
    int result = 0;
    if (ops->init) {
        result = ops->init(private_data);
        if (result == 0) {
            driver->state = DRIVER_LOADED;
        } else {
            driver->state = DRIVER_FAILED;
        }
    } else {
        driver->state = DRIVER_LOADED;
    }

    g_registry.driver_count++;
    pthread_mutex_unlock(&g_registry.registry_lock);

    if (result == 0) {
        printf("[DriverRegistry] Registered driver: %s (state: %s)\n",
               name, driver_state_to_string(driver->state));
    } else {
        fprintf(stderr, "[DriverRegistry] Driver init failed: %s\n", name);
    }

    return result;
}

int driver_unregister(const char* name) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    if (!name) {
        fprintf(stderr, "[DriverRegistry] Invalid arguments\n");
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    /* Driver must not be active */
    if (driver->state == DRIVER_ACTIVE) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Cannot unregister active driver: %s\n", name);
        return -1;
    }

    /* Call cleanup if provided */
    if (driver->ops && driver->ops->cleanup) {
        driver->ops->cleanup(driver->private_data);
    }

    /* Destroy driver lock */
    pthread_mutex_destroy(&driver->lock);

    /* Remove driver by shifting array */
    uint32_t index = driver - g_registry.drivers;
    memmove(&g_registry.drivers[index],
            &g_registry.drivers[index + 1],
            (g_registry.driver_count - index - 1) * sizeof(driver_info_t));
    g_registry.driver_count--;

    pthread_mutex_unlock(&g_registry.registry_lock);

    printf("[DriverRegistry] Unregistered driver: %s\n", name);
    return 0;
}

int driver_start(const char* name) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Validate state transition: LOADED -> ACTIVE */
    if (driver->state != DRIVER_LOADED) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Invalid state for start: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Call driver start */
    int result = 0;
    if (driver->ops && driver->ops->start) {
        result = driver->ops->start(driver->private_data);
    }

    if (result == 0) {
        driver->state = DRIVER_ACTIVE;
        driver->last_activity_time = get_timestamp_ms();
        printf("[DriverRegistry] Started driver: %s\n", name);
    } else {
        driver->state = DRIVER_FAILED;
        fprintf(stderr, "[DriverRegistry] Driver start failed: %s\n", name);
    }

    pthread_mutex_unlock(&driver->lock);
    return result;
}

int driver_stop(const char* name) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Validate state transition: ACTIVE -> LOADED */
    if (driver->state != DRIVER_ACTIVE) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Invalid state for stop: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Call driver stop */
    int result = 0;
    if (driver->ops && driver->ops->stop) {
        result = driver->ops->stop(driver->private_data);
    }

    if (result == 0) {
        driver->state = DRIVER_LOADED;
        printf("[DriverRegistry] Stopped driver: %s\n", name);
    } else {
        driver->state = DRIVER_FAILED;
        fprintf(stderr, "[DriverRegistry] Driver stop failed: %s\n", name);
    }

    pthread_mutex_unlock(&driver->lock);
    return result;
}

int driver_suspend(const char* name) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Validate state transition: ACTIVE -> SUSPENDED */
    if (driver->state != DRIVER_ACTIVE) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Invalid state for suspend: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Call driver suspend */
    int result = 0;
    if (driver->ops && driver->ops->suspend) {
        result = driver->ops->suspend(driver->private_data);
    }

    if (result == 0) {
        driver->state = DRIVER_SUSPENDED;
        printf("[DriverRegistry] Suspended driver: %s\n", name);
    } else {
        driver->state = DRIVER_FAILED;
        fprintf(stderr, "[DriverRegistry] Driver suspend failed: %s\n", name);
    }

    pthread_mutex_unlock(&driver->lock);
    return result;
}

int driver_resume(const char* name) {
    if (!g_registry_initialized) {
        fprintf(stderr, "[DriverRegistry] Not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Validate state transition: SUSPENDED -> ACTIVE */
    if (driver->state != DRIVER_SUSPENDED) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Invalid state for resume: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Call driver resume */
    int result = 0;
    if (driver->ops && driver->ops->resume) {
        result = driver->ops->resume(driver->private_data);
    }

    if (result == 0) {
        driver->state = DRIVER_ACTIVE;
        driver->last_activity_time = get_timestamp_ms();
        printf("[DriverRegistry] Resumed driver: %s\n", name);
    } else {
        driver->state = DRIVER_FAILED;
        fprintf(stderr, "[DriverRegistry] Driver resume failed: %s\n", name);
    }

    pthread_mutex_unlock(&driver->lock);
    return result;
}

int driver_get_info(const char* name, driver_info_t* info) {
    if (!g_registry_initialized || !name || !info) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    memcpy(info, driver, sizeof(driver_info_t));
    pthread_mutex_unlock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    return 0;
}

int driver_get_stats(const char* name, driver_stats_t* stats) {
    if (!g_registry_initialized || !name || !stats) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    memcpy(stats, &driver->stats, sizeof(driver_stats_t));
    pthread_mutex_unlock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    return 0;
}

int driver_list(char names[][32], uint32_t max_count, uint32_t* count) {
    if (!g_registry_initialized || !names || !count) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    uint32_t num_drivers = g_registry.driver_count < max_count ?
                           g_registry.driver_count : max_count;

    for (uint32_t i = 0; i < num_drivers; i++) {
        strncpy(names[i], g_registry.drivers[i].name, 32);
        names[i][31] = '\0';
    }

    *count = num_drivers;
    pthread_mutex_unlock(&g_registry.registry_lock);

    return 0;
}

int driver_read(const char* name, uint64_t lba, uint32_t count, void* buffer) {
    if (!g_registry_initialized || !name || !buffer || count == 0) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Driver must be active */
    if (driver->state != DRIVER_ACTIVE) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Driver not active: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Check if driver supports read */
    if (!driver->ops || !driver->ops->read) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Driver does not support read: %s\n", name);
        return -1;
    }

    /* Perform read */
    uint64_t start_time = get_timestamp_ms();
    int result = driver->ops->read(driver->private_data, lba, count, buffer);
    uint64_t end_time = get_timestamp_ms();

    /* Update statistics */
    driver->stats.total_requests++;
    if (result >= 0) {
        driver->stats.successful_requests++;
        driver->stats.bytes_read += result;

        /* Update average latency */
        double latency = (double)(end_time - start_time);
        double total_latency = driver->stats.avg_latency_ms * (driver->stats.successful_requests - 1);
        driver->stats.avg_latency_ms = (total_latency + latency) / driver->stats.successful_requests;
    } else {
        driver->stats.failed_requests++;
        driver->stats.errors++;
    }

    driver->last_activity_time = end_time;
    pthread_mutex_unlock(&driver->lock);

    return result;
}

int driver_write(const char* name, uint64_t lba, uint32_t count, const void* buffer) {
    if (!g_registry_initialized || !name || !buffer || count == 0) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    driver_info_t* driver = find_driver_unlocked(name);
    if (!driver) {
        pthread_mutex_unlock(&g_registry.registry_lock);
        fprintf(stderr, "[DriverRegistry] Driver not found: %s\n", name);
        return -1;
    }

    pthread_mutex_lock(&driver->lock);
    pthread_mutex_unlock(&g_registry.registry_lock);

    /* Driver must be active */
    if (driver->state != DRIVER_ACTIVE) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Driver not active: %s (state: %s)\n",
                name, driver_state_to_string(driver->state));
        return -1;
    }

    /* Check if driver supports write */
    if (!driver->ops || !driver->ops->write) {
        pthread_mutex_unlock(&driver->lock);
        fprintf(stderr, "[DriverRegistry] Driver does not support write: %s\n", name);
        return -1;
    }

    /* Perform write */
    uint64_t start_time = get_timestamp_ms();
    int result = driver->ops->write(driver->private_data, lba, count, buffer);
    uint64_t end_time = get_timestamp_ms();

    /* Update statistics */
    driver->stats.total_requests++;
    if (result >= 0) {
        driver->stats.successful_requests++;
        driver->stats.bytes_written += result;

        /* Update average latency */
        double latency = (double)(end_time - start_time);
        double total_latency = driver->stats.avg_latency_ms * (driver->stats.successful_requests - 1);
        driver->stats.avg_latency_ms = (total_latency + latency) / driver->stats.successful_requests;
    } else {
        driver->stats.failed_requests++;
        driver->stats.errors++;
    }

    driver->last_activity_time = end_time;
    pthread_mutex_unlock(&driver->lock);

    return result;
}

int driver_registry_cleanup(void) {
    if (!g_registry_initialized) {
        return -1;
    }

    pthread_mutex_lock(&g_registry.registry_lock);

    /* Stop and cleanup all drivers */
    for (uint32_t i = 0; i < g_registry.driver_count; i++) {
        driver_info_t* driver = &g_registry.drivers[i];

        pthread_mutex_lock(&driver->lock);

        /* Stop driver if active */
        if (driver->state == DRIVER_ACTIVE && driver->ops && driver->ops->stop) {
            driver->ops->stop(driver->private_data);
        }

        /* Cleanup driver */
        if (driver->ops && driver->ops->cleanup) {
            driver->ops->cleanup(driver->private_data);
        }

        pthread_mutex_unlock(&driver->lock);
        pthread_mutex_destroy(&driver->lock);
    }

    g_registry.driver_count = 0;
    pthread_mutex_unlock(&g_registry.registry_lock);
    pthread_mutex_destroy(&g_registry.registry_lock);

    g_registry_initialized = false;
    printf("[DriverRegistry] Cleanup complete\n");
    return 0;
}

const char* driver_state_to_string(driver_state_t state) {
    switch (state) {
        case DRIVER_UNLOADED:  return "UNLOADED";
        case DRIVER_LOADED:    return "LOADED";
        case DRIVER_ACTIVE:    return "ACTIVE";
        case DRIVER_SUSPENDED: return "SUSPENDED";
        case DRIVER_FAILED:    return "FAILED";
        default:               return "UNKNOWN";
    }
}
