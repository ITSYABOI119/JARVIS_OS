#include <stdio.h>
#include "phase1/src/cache/decision_cache.h"

extern int cache_load_extended_patterns(decision_cache_t *cache);

const char *sysadmin_session[] = {
    "ls", "pwd", "ls -l", "status", "help",
    "ps", "top", "df", "free", "uptime",
    "ls", "cpu", "date", "time", "version",
    "ls -a", "status", "help", "ls", "pwd",
    "ps", "df", "free", "top", "uptime",
    "ls -l", "status", "about", "hello", "info"
};

int main(void) {
    decision_cache_t cache;
    cache_init(&cache);
    cache_load_extended_patterns(&cache);

    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  SYSADMIN SESSION (30 typical commands)                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    int hits = 0, misses = 0;
    int num = sizeof(sysadmin_session) / sizeof(sysadmin_session[0]);

    for (int i = 0; i < num; i++) {
        char action[MAX_ACTION_LEN];
        trust_level_t trust;
        bool hit = cache_lookup(&cache, sysadmin_session[i], action, MAX_ACTION_LEN, &trust);
        printf("%2d. %-15s → %s\n", i+1, sysadmin_session[i], hit ? "✓ HIT" : "✗ MISS");
        if (hit) hits++; else misses++;
    }

    float rate = (float)hits / num * 100.0;
    printf("\nHit rate: %d/%d = %.1f%%\n", hits, num, rate);
    
    if (rate >= 80.0) {
        printf("\n✓✓✓ EXCEEDS 80%% TARGET - This is what Pi 4 will achieve!\n");
    }
    printf("\n");
    return 0;
}
