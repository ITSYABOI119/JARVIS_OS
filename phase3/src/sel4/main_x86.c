/*
 * JARVIS AI-OS Phase 3 — x86-64 Rootserver
 * Minimal seL4 rootserver: banner, decision cache, demo queries.
 */

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <sel4platsupport/platsupport.h>

#include "decision_cache.h"
#include "cache_patterns.h"

/* ---- Serial output via seL4 debug syscall ---- */

static void puts_serial(const char *s)
{
    while (*s)
        seL4_DebugPutChar(*s++);
}

static void put_dec(uint32_t val)
{
    char buf[12];
    int i = 0;
    if (val == 0) { seL4_DebugPutChar('0'); return; }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        seL4_DebugPutChar(buf[i]);
}

/* ---- Globals ---- */

static decision_cache_t g_cache;
static uint32_t total_queries = 0;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;

static void do_query(const char *query)
{
    char action[256];
    trust_level_t trust;

    total_queries++;
    bool hit = cache_lookup(&g_cache, query, action, sizeof(action), &trust);
    if (hit) {
        cache_hits++;
        puts_serial("  [HIT]  ");
        puts_serial(query);
        puts_serial(" -> ");
        puts_serial(action);
        puts_serial("\n");
    } else {
        cache_misses++;
        puts_serial("  [MISS] ");
        puts_serial(query);
        puts_serial("\n");
    }
}

static void shield_check(const char *query)
{
    const char *bad[] = {"delete", "remove", "kill", "destroy", "format", "rm -rf", NULL};
    int blocked = 0;
    for (int i = 0; bad[i]; i++) {
        if (strstr(query, bad[i])) { blocked = 1; break; }
    }
    puts_serial("  [SHIELD] ");
    puts_serial(query);
    if (blocked)
        puts_serial(" -> BLOCKED\n");
    else
        puts_serial(" -> ALLOWED\n");
}

/* ---- Main ---- */

int main(void)
{
    seL4_BootInfo *info = platsupport_get_bootinfo();

    puts_serial("\n\n");
    puts_serial("==================================================\n");
    puts_serial("  JARVIS AI-OS v0.3.0-dev | seL4 x86-64 | PC99\n");
    puts_serial("==================================================\n\n");

    if (info) {
        puts_serial("Boot: ");
        put_dec(info->untyped.end - info->untyped.start);
        puts_serial(" untypeds\n");
    }

    /* Init cache */
    puts_serial("\n[JARVIS] Init decision cache...\n");
    cache_init(&g_cache);
    int n1 = cache_load_initial_patterns(&g_cache);
    int n2 = cache_load_extended_patterns(&g_cache);
    puts_serial("[JARVIS] Loaded ");
    put_dec((uint32_t)(n1 + n2));
    puts_serial(" patterns\n\n");

    /* Demo queries */
    puts_serial("--- Cache Queries ---\n");
    do_query("what is the system status");
    do_query("check disk space");
    do_query("list running processes");
    do_query("show memory usage");
    do_query("restart the web server");
    do_query("unknown query test");
    do_query("show network interfaces");
    do_query("open text editor");

    /* SHIELD */
    puts_serial("\n--- SHIELD ---\n");
    shield_check("check health");
    shield_check("delete all data");
    shield_check("rm -rf /");

    /* Stats */
    puts_serial("\n--- Stats ---\n");
    puts_serial("  Queries: "); put_dec(total_queries); puts_serial("\n");
    puts_serial("  Hits:    "); put_dec(cache_hits); puts_serial("\n");
    puts_serial("  Misses:  "); put_dec(cache_misses); puts_serial("\n");
    if (total_queries > 0) {
        puts_serial("  Rate:    ");
        put_dec((cache_hits * 100) / total_queries);
        puts_serial("%\n");
    }

    puts_serial("\n[JARVIS] Done. System idle.\n");

    while (1) { seL4_Yield(); }
    return 0;
}
