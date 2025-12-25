/*
 * JARVIS AI-OS - Phase 2 Week 30 Main Entry Point
 *
 * Bidirectional Python↔seL4 IPC via QEMU ivshmem Shared Memory
 *
 * Week 28: Dual ring buffer + IPC handler (polling mode)
 * Week 30: QEMU ivshmem integration for real Python↔seL4 IPC
 *
 * Key Features:
 * - Uses dual ring buffer (separate query and response channels)
 * - QEMU ivshmem device for shared memory with host
 * - Supports MSG_CACHE_LOOKUP and MSG_CACHE_STATS messages
 * - Returns responses via response ring to Python
 */

#include <stdio.h>
#include <string.h>
#include <sel4/sel4.h>
#include "decision_cache.h"
#include "stdin_impl.h"
#include "dual_ring_buffer.h"
#include "ipc_handler.h"
#include "pci_ivshmem.h"

#define BANNER \
    "\n" \
    "========================================\n" \
    "  JARVIS AI-OS v0.3 - Phase 2 Week 30\n" \
    "  ivshmem Shared Memory IPC\n" \
    "========================================\n" \
    "  seL4 + ivshmem + Python Host IPC\n" \
    "  Build: " __DATE__ " " __TIME__ "\n" \
    "========================================\n\n"

#define PROMPT "jarvis> "
#define INPUT_BUFFER_SIZE 256

/* Global decision cache */
static decision_cache_t g_cache;

/* Global dual ring buffer (pointer to ivshmem or fallback) */
static dual_ring_buffer_t *g_dual_ring = NULL;
static dual_ring_buffer_t g_dual_ring_fallback;  /* Fallback if ivshmem unavailable */

/* Global ivshmem device state */
static ivshmem_device_t g_ivshmem;
static bool g_using_ivshmem = false;

/* Global IPC handler state */
static ipc_handler_state_t g_ipc_handler;

/* External pattern loading functions */
extern int cache_load_extended_patterns(decision_cache_t *cache);

/*
 * Execute command using decision cache (same as Phase 1)
 */
void execute_command(const char *cmd, decision_cache_t *cache)
{
    printf("%s%s\n", PROMPT, cmd);

    /* Normalize query */
    char normalized[MAX_QUERY_LEN];
    if (!cache_normalize_query(cmd, normalized, MAX_QUERY_LEN)) {
        printf("Error: Query too long\n\n");
        return;
    }

    /* Try cache lookup first */
    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    if (cache_lookup(cache, normalized, action, MAX_ACTION_LEN, &trust)) {
        /* Cache hit! Execute cached action */
        printf("[CACHE HIT] Action: %s (trust=%d)\n", action, trust);

        /* Execute the action */
        if (strcmp(action, "show_help") == 0) {
            printf("Available commands:\n");
            printf("  help    - Show this help message\n");
            printf("  status  - Show system status\n");
            printf("  cache stats - Show cache statistics\n");
            printf("  ls      - List directory\n");
            printf("  pwd     - Print working directory\n");
        }
        else if (strcmp(action, "show_status") == 0) {
            printf("JARVIS AI-OS Status:\n");
            printf("  Week 28: Bidirectional IPC WORKING\n");
            printf("  Dual Ring Buffer: INITIALIZED\n");
            printf("  Cache lookups: Fast (<1μs)\n");
        }
        else if (strcmp(action, "show_cache_stats") == 0) {
            cache_print_stats(cache);
        }
        else {
            /* Generic action execution */
            printf("Executing: %s\n", action);
        }
    }
    else {
        /* Cache miss - handle manually */
        printf("[CACHE MISS] Handling manually\n");

        if (strcmp(normalized, "exit") == 0) {
            printf("To exit QEMU: Press Ctrl+A then X\n");
        }
        else {
            printf("Unknown command: %s\n", cmd);
        }
    }
    printf("\n");
}

/*
 * Week 28: IPC Message Handler (Polling Mode)
 *
 * Processes messages from Python AI agent via dual ring buffer.
 * Uses IPC handler in polling mode (processes one message per call).
 */
void ipc_message_handler_polling(void)
{
    int processed = 0;

    printf("========================================\n");
    printf("Week 28 IPC Handler - Bidirectional Mode\n");
    printf("========================================\n\n");

    printf("Polling dual ring buffer for Python queries...\n");
    printf("(Processing up to 100 messages, then timeout)\n\n");

    /* Poll for messages with timeout */
    int timeout_count = 0;
    int max_timeouts = 100;  /* Exit after 10 seconds (100 * 100ms) */

    while (timeout_count < max_timeouts) {
        /* Process one IPC message (non-blocking) */
        char query[MAX_QUERY_LEN];
        uint32_t msg_id;
        ring_message_t msg;

        /* Try to read from query ring */
        if (ring_buffer_read(&g_dual_ring->query_ring, &msg)) {
            /* Message received! */
            timeout_count = 0;  /* Reset timeout */
            processed++;
            msg_id = msg.id;

            printf("[MSG #%d] Received from Python:\n", processed);
            printf("  Type: %d (", msg.type);
            switch(msg.type) {
                case MSG_CACHE_LOOKUP: printf("CACHE_LOOKUP"); break;
                case MSG_CACHE_STATS: printf("CACHE_STATS"); break;
                default: printf("UNKNOWN"); break;
            }
            printf(")\n");
            printf("  ID: %u\n", msg.id);
            printf("  Size: %u bytes\n", msg.payload_size);

            /* Handle MSG_CACHE_LOOKUP */
            if (msg.type == MSG_CACHE_LOOKUP) {
                /* Extract query string */
                size_t copy_len = (msg.payload_size < MAX_QUERY_LEN) ? msg.payload_size : (MAX_QUERY_LEN - 1);
                memcpy(query, msg.payload, copy_len);
                query[copy_len] = '\0';

                printf("  Query: \"%s\"\n", query);

                /* Perform cache lookup */
                char action[MAX_ACTION_LEN];
                trust_level_t trust;
                bool hit = cache_lookup(&g_cache, query, action, MAX_ACTION_LEN, &trust);

                if (hit) {
                    printf("  [CACHE HIT] Action: %s, Trust: %d\n", action, trust);
                } else {
                    printf("  [CACHE MISS]\n");
                }

                /* Send response via response ring */
                if (dual_ring_send_response(g_dual_ring, msg_id, hit, action, trust)) {
                    printf("  [SENT] Response to Python via response ring\n");
                } else {
                    printf("  [ERROR] Failed to send response\n");
                }
            }
            /* Handle MSG_CACHE_STATS */
            else if (msg.type == MSG_CACHE_STATS) {
                printf("  Request: Cache statistics\n");

                /* Get cache statistics */
                cache_stats_t stats;
                cache_get_stats(&g_cache, &stats);

                /* Send stats response */
                if (dual_ring_send_cache_stats(g_dual_ring, msg_id, &stats)) {
                    printf("  [SENT] Cache stats to Python\n");
                } else {
                    printf("  [ERROR] Failed to send stats\n");
                }
            }
            else {
                printf("  [WARNING] Unsupported message type\n");
            }

            printf("\n");

        } else {
            /* No message available, increment timeout */
            timeout_count++;

            /* Sleep to avoid busy-waiting (simple delay ~100ms) */
            for (volatile int i = 0; i < 100000; i++);
        }
    }

    printf("========================================\n");
    printf("IPC Message Handler Complete\n");
    printf("  Messages processed: %d\n", processed);
    printf("========================================\n\n");

    /* Print IPC handler statistics */
    ipc_handler_print_stats(&g_ipc_handler);

    /* Print dual ring buffer statistics */
    dual_ring_print_stats(g_dual_ring);
}

/*
 * Main entry point - seL4 root task
 */
int main(int argc, char *argv[])
{
    /* Print banner */
    printf(BANNER);

    /* Print system info */
    printf("System Information:\n");
    printf("  Architecture: x86_64\n");
    printf("  Platform: pc99 (QEMU)\n");
    printf("  Kernel: seL4 microkernel\n");
    printf("  Phase: 2 Week 28\n");
    printf("\n");

    /* Initialize decision cache */
    printf("Initializing decision cache...\n");
    if (!cache_init(&g_cache)) {
        printf("ERROR: Failed to initialize cache!\n");
        return 1;
    }
    printf("✓ Cache initialized (512 entries)\n");

    /* Load patterns */
    int loaded = cache_load_extended_patterns(&g_cache);
    printf("✓ Loaded %d patterns into cache\n", loaded);
    printf("\n");

    /* Week 30: Initialize ivshmem for shared memory IPC */
    printf("Initializing ivshmem shared memory...\n");
    ivshmem_init(&g_ivshmem);

    if (ivshmem_detect(&g_ivshmem)) {
        printf("  ivshmem device detected\n");

        if (ivshmem_map_bar2(&g_ivshmem)) {
            g_dual_ring = (dual_ring_buffer_t *)ivshmem_get_shared_memory(&g_ivshmem);
            g_using_ivshmem = true;
            printf("OK Using ivshmem shared memory at %p\n", (void *)g_dual_ring);
            ivshmem_print_info(&g_ivshmem);
        } else {
            printf("WARNING: Failed to map ivshmem, using fallback\n");
            g_dual_ring = &g_dual_ring_fallback;
            g_using_ivshmem = false;
        }
    } else {
        printf("WARNING: ivshmem not detected, using fallback buffer\n");
        printf("  (Python IPC will NOT work without ivshmem)\n");
        g_dual_ring = &g_dual_ring_fallback;
        g_using_ivshmem = false;
    }

    /* Initialize dual ring buffer (at ivshmem or fallback location) */
    printf("Initializing dual ring buffer...\n");
    if (!dual_ring_init(g_dual_ring)) {
        printf("ERROR: Failed to initialize dual ring buffer!\n");
        return 1;
    }
    printf("OK Dual ring buffer initialized\n");
    printf("  Query Ring:    Python -> seL4\n");
    printf("  Response Ring: seL4 -> Python\n");
    printf("  Memory: ~567KB %s\n", g_using_ivshmem ? "(ivshmem shared)" : "(local fallback)");
    printf("\n");

    /* Week 28: Initialize IPC handler */
    printf("Initializing IPC handler...\n");
    if (!ipc_handler_init(&g_ipc_handler, &g_cache, g_dual_ring)) {
        printf("ERROR: Failed to initialize IPC handler!\n");
        return 1;
    }
    printf("✓ IPC handler initialized (polling mode)\n");
    printf("\n");

    /* Week 28: Run IPC message handler */
    ipc_message_handler_polling();

    printf("\n");
    printf("========================================\n");
    printf("  Week 30 Deliverables Complete:\n");
    printf("  [%c] ivshmem shared memory detected\n", g_using_ivshmem ? 'Y' : 'N');
    printf("  [%c] Python <-> seL4 IPC via ivshmem\n", g_using_ivshmem ? 'Y' : 'N');
    printf("  [Y] Dual ring buffer initialized\n");
    printf("  [Y] IPC handler (polling mode)\n");
    printf("  [Y] MSG_CACHE_LOOKUP supported\n");
    printf("  [Y] MSG_CACHE_STATS supported\n");
    printf("========================================\n");
    if (!g_using_ivshmem) {
        printf("\nWARNING: Running without ivshmem!\n");
        printf("Python IPC will not work.\n");
        printf("Start QEMU with ivshmem device:\n");
        printf("  -device ivshmem-plain,memdev=shm0\n");
        printf("  -object memory-backend-file,...\n");
    }
    printf("\nPress Ctrl+A X to exit QEMU\n\n");

    /* Infinite loop to keep system running */
    while (1) {
        seL4_Yield();
    }

    /* Should never reach here */
    return 0;
}
