/*
 * JARVIS AI-OS - Combined source file for seL4 tutorials framework
 * Phase 2 Week 30 - ivshmem Integration
 *
 * This file includes all JARVIS source files to work within the
 * single-file constraint of the seL4 tutorials build system.
 */

/* Include all source files directly */
#include "decision_cache.c"
#include "cache_patterns.c"
#include "ring_buffer.c"
#include "ipc_sel4.c"
#include "dual_ring_buffer.c"
#include "ipc_handler.c"
#include "pci_ivshmem.c"

/* Now include our main entry point (without the redundant includes) */
#define JARVIS_COMBINED_BUILD 1

/* Main entry point code from main_week28.c follows */
#include <stdio.h>
#include <string.h>
#include <sel4/sel4.h>

#define BANNER \
    "\n" \
    "========================================\n" \
    "  JARVIS AI-OS v0.3 - Phase 2 Week 30\n" \
    "  ivshmem Shared Memory IPC\n" \
    "========================================\n" \
    "  seL4 + ivshmem + Python Host IPC\n" \
    "  Build: " __DATE__ " " __TIME__ "\n" \
    "========================================\n\n"

/* Global state */
static decision_cache_t g_cache;
static dual_ring_buffer_t *g_dual_ring = NULL;
static dual_ring_buffer_t g_dual_ring_fallback;
static ivshmem_device_t g_ivshmem;
static bool g_using_ivshmem = false;
static ipc_handler_state_t g_ipc_handler;

extern int cache_load_extended_patterns(decision_cache_t *cache);

void execute_command(const char *cmd, decision_cache_t *cache)
{
    printf("jarvis> %s\n", cmd);

    char normalized[MAX_QUERY_LEN];
    if (!cache_normalize_query(cmd, normalized, MAX_QUERY_LEN)) {
        printf("Error: Query too long\n\n");
        return;
    }

    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    if (cache_lookup(cache, normalized, action, MAX_ACTION_LEN, &trust)) {
        printf("[CACHE HIT] Action: %s (trust=%d)\n", action, trust);

        if (strcmp(action, "show_help") == 0) {
            printf("Available commands: help, status, cache stats, ls, pwd\n");
        }
        else if (strcmp(action, "show_status") == 0) {
            printf("JARVIS AI-OS Status: Week 30 ivshmem IPC\n");
        }
        else if (strcmp(action, "show_cache_stats") == 0) {
            cache_print_stats(cache);
        }
        else {
            printf("Executing: %s\n", action);
        }
    }
    else {
        printf("[CACHE MISS] Unknown: %s\n", cmd);
    }
    printf("\n");
}

void ipc_message_handler_polling(void)
{
    int processed = 0;

    printf("========================================\n");
    printf("Week 30 IPC Handler - ivshmem Mode\n");
    printf("========================================\n\n");

    printf("Polling for Python queries (10s timeout)...\n\n");

    int timeout_count = 0;
    int max_timeouts = 100;

    while (timeout_count < max_timeouts) {
        ring_message_t msg;

        if (ring_buffer_read(&g_dual_ring->query_ring, &msg)) {
            timeout_count = 0;
            processed++;

            printf("[MSG #%d] Type=%d ID=%u\n", processed, msg.type, msg.id);

            if (msg.type == MSG_CACHE_LOOKUP) {
                char query[MAX_QUERY_LEN];
                size_t copy_len = (msg.payload_size < MAX_QUERY_LEN) ? msg.payload_size : (MAX_QUERY_LEN - 1);
                memcpy(query, msg.payload, copy_len);
                query[copy_len] = '\0';

                printf("  Query: \"%s\"\n", query);

                char action[MAX_ACTION_LEN];
                trust_level_t trust;
                bool hit = cache_lookup(&g_cache, query, action, MAX_ACTION_LEN, &trust);

                if (hit) {
                    printf("  [CACHE HIT] %s (trust=%d)\n", action, trust);
                } else {
                    printf("  [CACHE MISS]\n");
                }

                if (dual_ring_send_response(g_dual_ring, msg.id, hit, action, trust)) {
                    printf("  [SENT] Response to Python\n");
                }
            }
            else if (msg.type == MSG_CACHE_STATS) {
                cache_stats_t stats;
                cache_get_stats(&g_cache, &stats);
                if (dual_ring_send_cache_stats(g_dual_ring, msg.id, &stats)) {
                    printf("  [SENT] Cache stats\n");
                }
            }
            printf("\n");
        } else {
            timeout_count++;
            for (volatile int i = 0; i < 100000; i++);
        }
    }

    printf("========================================\n");
    printf("IPC Complete: %d messages processed\n", processed);
    printf("========================================\n\n");

    ipc_handler_print_stats(&g_ipc_handler);
    dual_ring_print_stats(g_dual_ring);
}

int main(int argc, char *argv[])
{
    printf(BANNER);

    printf("System Information:\n");
    printf("  Architecture: x86_64\n");
    printf("  Platform: pc99 (QEMU)\n");
    printf("  Kernel: seL4 microkernel\n");
    printf("  Phase: 2 Week 30\n\n");

    printf("Initializing decision cache...\n");
    if (!cache_init(&g_cache)) {
        printf("ERROR: Cache init failed!\n");
        return 1;
    }
    printf("OK Cache initialized (512 entries)\n");

    int loaded = cache_load_extended_patterns(&g_cache);
    printf("OK Loaded %d patterns\n\n", loaded);

    printf("Initializing ivshmem shared memory...\n");
    ivshmem_init(&g_ivshmem);

    if (ivshmem_detect(&g_ivshmem)) {
        printf("  ivshmem device detected\n");

        if (ivshmem_map_bar2(&g_ivshmem)) {
            g_dual_ring = (dual_ring_buffer_t *)ivshmem_get_shared_memory(&g_ivshmem);
            g_using_ivshmem = true;
            printf("OK Using ivshmem at %p\n", (void *)g_dual_ring);
            ivshmem_print_info(&g_ivshmem);
        } else {
            printf("WARNING: Failed to map ivshmem\n");
            g_dual_ring = &g_dual_ring_fallback;
            g_using_ivshmem = false;
        }
    } else {
        printf("WARNING: ivshmem not detected (fallback mode)\n");
        g_dual_ring = &g_dual_ring_fallback;
        g_using_ivshmem = false;
    }

    printf("Initializing dual ring buffer...\n");
    if (!dual_ring_init(g_dual_ring)) {
        printf("ERROR: Dual ring init failed!\n");
        return 1;
    }
    printf("OK Dual ring buffer initialized\n");
    printf("  Query Ring:    Python -> seL4\n");
    printf("  Response Ring: seL4 -> Python\n");
    printf("  Mode: %s\n\n", g_using_ivshmem ? "ivshmem shared" : "local fallback");

    printf("Initializing IPC handler...\n");
    if (!ipc_handler_init(&g_ipc_handler, &g_cache, g_dual_ring)) {
        printf("ERROR: IPC handler init failed!\n");
        return 1;
    }
    printf("OK IPC handler initialized\n\n");

    ipc_message_handler_polling();

    printf("\n========================================\n");
    printf("  Week 30 Deliverables:\n");
    printf("  [%c] ivshmem shared memory\n", g_using_ivshmem ? 'Y' : 'N');
    printf("  [%c] Python <-> seL4 IPC\n", g_using_ivshmem ? 'Y' : 'N');
    printf("  [Y] Dual ring buffer\n");
    printf("  [Y] IPC handler (polling)\n");
    printf("========================================\n");

    if (!g_using_ivshmem) {
        printf("\nWARNING: Running without ivshmem!\n");
        printf("Add to QEMU: -device ivshmem-plain,memdev=shm0\n");
    }
    printf("\nPress Ctrl+A X to exit QEMU\n\n");

    while (1) {
        seL4_Yield();
    }

    return 0;
}
