/*
 * JARVIS AI-OS Phase 3 — x86-64 Rootserver
 *
 * Minimal seL4 rootserver for x86-64 PC99 platform.
 * Boots, initializes serial console, loads decision cache,
 * and runs an interactive shell over serial.
 *
 * Build: Integrate into seL4 build system (see CMakeLists.txt)
 * Run:   QEMU x86-64 with -nographic -serial mon:stdio
 *
 * Phase 3b target: standalone JARVIS OS on bare-metal x86.
 * This file is the x86 equivalent of phase2/src/sel4/main_arm64.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4utils/util.h>

/* Phase 2 portable modules */
#include "decision_cache.h"
#include "cache_patterns.h"

/*============================================================
 * Platform: x86-64 PC99
 *
 * Unlike ARM64 (Phase 2), x86 serial is at I/O port 0x3F8,
 * not memory-mapped MMIO. seL4_DebugPutChar() works for text
 * output via the kernel debug serial (COM1).
 *
 * For Phase 3b production:
 *   - Replace seL4_DebugPutChar with direct 16550A I/O port access
 *   - Add AHCI storage driver for model file loading
 *   - Add NIC driver for networking
 *   - Add shared memory IPC for AI inference process
 *============================================================*/

/* ---- Serial output (via seL4 debug syscall, same as Phase 2 text path) ---- */

static void sel4_putc(char c)
{
    seL4_DebugPutChar(c);
}

static void sel4_puts(const char *s)
{
    while (*s)
        sel4_putc(*s++);
}

static void sel4_put_hex32(uint32_t val)
{
    const char hex[] = "0123456789abcdef";
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    sel4_puts(buf);
}

static void sel4_put_dec(uint32_t val)
{
    char buf[12];
    int i = 0;
    if (val == 0) { sel4_putc('0'); return; }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        sel4_putc(buf[i]);
}

/* ---- Decision Cache (portable from Phase 2) ---- */

static DecisionCache g_cache;

static void init_cache(void)
{
    sel4_puts("[JARVIS] Initializing decision cache...\n");
    decision_cache_init(&g_cache);
    int loaded = cache_load_default_patterns(&g_cache);
    sel4_puts("[JARVIS] Cache loaded: ");
    sel4_put_dec(loaded);
    sel4_puts(" patterns\n");
}

static void cache_lookup(const char *query)
{
    CacheResult result;
    int hit = decision_cache_lookup(&g_cache, query, &result);

    if (hit) {
        sel4_puts("[CACHE HIT] ");
        sel4_puts(result.action);
        sel4_puts("\n");
    } else {
        sel4_puts("[CACHE MISS] No cached response for: ");
        sel4_puts(query);
        sel4_puts("\n");
        sel4_puts("  (Would forward to AI inference in full system)\n");
    }
}

/* ---- SHIELD Safety Check (simplified) ---- */

static float shield_risk_score(const char *query)
{
    /* Simple keyword-based risk scoring (portable from Phase 2) */
    const char *dangerous[] = {
        "delete", "remove", "kill", "destroy", "format", "erase",
        "shutdown", "halt", "rm -rf", "drop table", NULL
    };

    for (int i = 0; dangerous[i]; i++) {
        /* Simple substring search */
        const char *p = query;
        const char *d = dangerous[i];
        int dlen = strlen(d);
        while (*p) {
            if (strncmp(p, d, dlen) == 0)
                return 0.85f; /* High risk */
            p++;
        }
    }
    return 0.1f; /* Low risk */
}

static void shield_check(const char *query)
{
    float risk = shield_risk_score(query);
    sel4_puts("[SHIELD] Risk score: ");

    /* Print risk as fixed-point (no floating point printf in muslc) */
    int whole = (int)(risk * 100);
    sel4_puts("0.");
    if (whole < 10) sel4_putc('0');
    sel4_put_dec(whole);

    if (risk > 0.7f) {
        sel4_puts(" -> BLOCKED (high risk)\n");
    } else if (risk > 0.3f) {
        sel4_puts(" -> WARNING (moderate risk)\n");
    } else {
        sel4_puts(" -> ALLOWED (low risk)\n");
    }
}

/* ---- Cache Statistics ---- */

static uint32_t total_queries = 0;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static uint32_t shield_blocks = 0;

static void print_stats(void)
{
    sel4_puts("\n=== JARVIS x86 Statistics ===\n");
    sel4_puts("  Total queries:  "); sel4_put_dec(total_queries); sel4_putc('\n');
    sel4_puts("  Cache hits:     "); sel4_put_dec(cache_hits); sel4_putc('\n');
    sel4_puts("  Cache misses:   "); sel4_put_dec(cache_misses); sel4_putc('\n');
    sel4_puts("  SHIELD blocks:  "); sel4_put_dec(shield_blocks); sel4_putc('\n');
    if (total_queries > 0) {
        uint32_t hit_pct = (cache_hits * 100) / total_queries;
        sel4_puts("  Hit rate:       "); sel4_put_dec(hit_pct); sel4_puts("%\n");
    }
    sel4_puts("============================\n\n");
}

/* ---- Shell Command Dispatch ---- */

static void dispatch_command(char *line)
{
    /* Skip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '\n') return;

    /* Remove trailing newline */
    int len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

    if (strcmp(line, "help") == 0) {
        sel4_puts("\nJARVIS x86 Commands:\n");
        sel4_puts("  query <text>  - Look up in decision cache\n");
        sel4_puts("  shield <text> - Run SHIELD risk check\n");
        sel4_puts("  stats         - Show cache statistics\n");
        sel4_puts("  cache info    - Show cache info\n");
        sel4_puts("  help          - Show this help\n");
        sel4_puts("  version       - Show version info\n\n");
    } else if (strcmp(line, "stats") == 0) {
        print_stats();
    } else if (strcmp(line, "version") == 0) {
        sel4_puts("\nJARVIS AI-OS v0.3.0-dev (Phase 3 x86-64)\n");
        sel4_puts("  Platform: PC99 (x86-64)\n");
        sel4_puts("  Kernel:   seL4 (formally verified)\n");
        sel4_puts("  Cache:    258 patterns\n");
        sel4_puts("  IPC:      Shared memory (pending)\n");
        sel4_puts("  AI:       ggml/llama.cpp (pending)\n\n");
    } else if (strcmp(line, "cache info") == 0) {
        sel4_puts("\nDecision Cache:\n");
        sel4_puts("  Patterns loaded: ");
        sel4_put_dec(g_cache.count);
        sel4_puts("\n  Hash algorithm:  FNV-1a 64-bit\n");
        sel4_puts("  Lookup time:     <1ms (O(1) hash)\n\n");
    } else if (strncmp(line, "query ", 6) == 0) {
        total_queries++;
        CacheResult result;
        if (decision_cache_lookup(&g_cache, line + 6, &result)) {
            cache_hits++;
            sel4_puts("[HIT] ");
            sel4_puts(result.action);
            sel4_putc('\n');
        } else {
            cache_misses++;
            sel4_puts("[MISS] ");
            sel4_puts(line + 6);
            sel4_putc('\n');
        }
    } else if (strncmp(line, "shield ", 7) == 0) {
        total_queries++;
        shield_check(line + 7);
        if (shield_risk_score(line + 7) > 0.7f)
            shield_blocks++;
    } else {
        /* Try as implicit cache query */
        total_queries++;
        CacheResult result;
        if (decision_cache_lookup(&g_cache, line, &result)) {
            cache_hits++;
            sel4_puts("[HIT] ");
            sel4_puts(result.action);
            sel4_putc('\n');
        } else {
            cache_misses++;
            sel4_puts("Unknown command: ");
            sel4_puts(line);
            sel4_puts("\n  Type 'help' for commands\n");
        }
    }
}

/* ---- Main Entry Point ---- */

int main(int argc, char *argv[])
{
    /* Get boot info from seL4 kernel */
    seL4_BootInfo *info = platsupport_get_bootinfo();

    /* Print banner */
    sel4_puts("\n");
    sel4_puts("     ██╗ █████╗ ██████╗ ██╗   ██╗██╗███████╗\n");
    sel4_puts("     ██║██╔══██╗██╔══██╗██║   ██║██║██╔════╝\n");
    sel4_puts("     ██║███████║██████╔╝██║   ██║██║███████╗\n");
    sel4_puts("██   ██║██╔══██║██╔══██╗╚██╗ ██╔╝██║╚════██║\n");
    sel4_puts("╚█████╔╝██║  ██║██║  ██║ ╚████╔╝ ██║███████║\n");
    sel4_puts(" ╚════╝ ╚═╝  ╚═╝╚═╝  ╚═╝  ╚═══╝  ╚═╝╚══════╝\n");
    sel4_puts("\n");
    sel4_puts("JARVIS AI-OS v0.3.0-dev | Phase 3 x86-64 | seL4 Microkernel\n");
    sel4_puts("Platform: PC99 | Architecture: x86-64\n");

    if (info) {
        sel4_puts("Boot info: ");
        sel4_put_dec(info->untyped.end - info->untyped.start);
        sel4_puts(" untypeds available\n");
    }

    sel4_puts("\n");

    /* Initialize decision cache */
    init_cache();

    /* Print ready message */
    sel4_puts("\n[JARVIS] System ready. Type 'help' for commands.\n");
    sel4_puts("jarvis> ");

    /*
     * NOTE: seL4 rootserver on x86 does NOT have direct serial RX
     * via seL4_DebugPutChar (which is TX only). For interactive input
     * we need either:
     *   a) A 16550A UART driver reading I/O port 0x3F8 (Phase 3b)
     *   b) seL4_DebugGetChar() if available in debug build
     *   c) A test harness that feeds commands via boot module
     *
     * For now, this rootserver demonstrates:
     *   - seL4 x86-64 boots and reaches userspace
     *   - Decision cache loads and is queryable
     *   - SHIELD risk scoring works
     *   - Shell framework is ready for input
     *
     * TODO (Phase 3b Week 13-14): Add 16550A UART RX for interactive shell
     */

    /* Run a few demo queries to prove functionality */
    sel4_puts("\n--- Running demo queries ---\n\n");

    dispatch_command("version");
    dispatch_command("query what is the system status");
    dispatch_command("query check disk space");
    dispatch_command("query list running processes");
    dispatch_command("shield delete all user data");
    dispatch_command("shield check system health");
    dispatch_command("stats");

    sel4_puts("[JARVIS] Demo complete. System halted.\n");
    sel4_puts("[JARVIS] Interactive shell requires 16550A UART RX driver (Phase 3b Week 13-14).\n");

    /* Halt — in production this would be the IPC main loop */
    while (1) {
        seL4_Yield();
    }

    return 0;
}
