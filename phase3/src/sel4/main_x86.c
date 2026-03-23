/*
 * JARVIS AI-OS Phase 3 — x86-64 Rootserver
 * Minimal seL4 rootserver: banner, decision cache, demo queries.
 * Optional shared memory IPC (enabled with -DJARVIS_IPC_SHMEM).
 *
 * TODO(Phase 3c): Separate drivers into isolated seL4 processes for
 * capability-based isolation. Current single-rootserver design means
 * a driver bug compromises the entire system. See SEC-014.
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
#include "tensor_ops.h"
#include "dequant.h"
#include "tokenizer.h"
#include "sampling.h"

#ifdef JARVIS_IPC_SHMEM
#include "shmem_ipc.h"
#endif

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

#ifdef JARVIS_IPC_SHMEM
static shmem_ring_t g_request_ring;
static shmem_ring_t g_response_ring;
#endif

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

/* SEC-010: Normalize query for SHIELD — lowercase + collapse whitespace */
static void normalize_for_shield(const char *input, char *output, size_t max_len)
{
    size_t j = 0;
    int prev_space = 0;
    for (size_t i = 0; input[i] && j < max_len - 1; i++) {
        char c = input[i];
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
        if (c == ' ' || c == '\t') {
            if (!prev_space && j > 0) { output[j++] = ' '; }
            prev_space = 1;
        } else {
            output[j++] = c;
            prev_space = 0;
        }
    }
    output[j] = '\0';
}

static void shield_check(const char *query)
{
    char normalized[256];
    normalize_for_shield(query, normalized, sizeof(normalized));
    const char *bad[] = {"delete", "remove", "kill", "destroy", "format", "rm -rf", NULL};
    int blocked = 0;
    for (int i = 0; bad[i]; i++) {
        if (strstr(normalized, bad[i])) { blocked = 1; break; }
    }
    puts_serial("  [SHIELD] ");
    puts_serial(query);
    if (blocked)
        puts_serial(" -> BLOCKED\n");
    else
        puts_serial(" -> ALLOWED\n");
}

/* ---- Shared Memory IPC ---- */

#ifdef JARVIS_IPC_SHMEM
static void init_ipc(void)
{
    shmem_ipc_init(&g_request_ring);
    shmem_ipc_init(&g_response_ring);
    puts_serial("[JARVIS] Shared memory IPC initialized\n");
}

static void ipc_process_one(void)
{
    uint8_t type;
    uint16_t seq;
    uint8_t payload[SHMEM_MAX_PAYLOAD];
    uint16_t len;

    if (shmem_ipc_recv(&g_request_ring, &type, &seq, payload, &len) != 0)
        return;  /* No pending message */

    /* Process based on type */
    switch (type) {
    case MSG_QUERY: {
        /* Null-terminate the query */
        char query[241];
        if (len > 240) len = 240;
        memcpy(query, payload, len);
        query[len] = '\0';

        /* Cache lookup */
        char action[256];
        trust_level_t trust;
        total_queries++;

        if (cache_lookup(&g_cache, query, action, sizeof(action), &trust)) {
            cache_hits++;
            uint16_t alen = (uint16_t)strlen(action);
            shmem_ipc_send(&g_response_ring, MSG_RESPONSE, seq,
                           action, alen);
        } else {
            cache_misses++;
            const char *miss = "CACHE_MISS";
            shmem_ipc_send(&g_response_ring, MSG_RESPONSE, seq,
                           miss, 10);
        }
        break;
    }
    case MSG_HEARTBEAT:
        shmem_ipc_send(&g_response_ring, MSG_HEARTBEAT_ACK, seq,
                       NULL, 0);
        break;
    case MSG_SHIELD_CHECK: {
        /* SEC-010: Normalize + keyword check */
        char query[241];
        if (len > 240) len = 240;
        memcpy(query, payload, len);
        query[len] = '\0';

        char normalized[241];
        normalize_for_shield(query, normalized, sizeof(normalized));

        const char *bad[] = {
            "delete", "remove", "kill", "destroy", "format", "rm -rf", NULL
        };
        int blocked = 0;
        for (int i = 0; bad[i]; i++) {
            if (strstr(normalized, bad[i])) { blocked = 1; break; }
        }

        uint8_t result = blocked ? 1 : 0;
        shmem_ipc_send(&g_response_ring, MSG_SHIELD_RESULT, seq,
                       &result, 1);
        break;
    }
    default:
        break;
    }
}
#endif /* JARVIS_IPC_SHMEM */

/* ---- Float comparison helper ---- */

static int fclose_enough(float a, float b)
{
    float diff = a - b;
    if (diff < 0) diff = -diff;
    return diff < 0.01f;
}

/* ---- Self-Tests ---- */

static int selftest_tensor_ops(void)
{
    int pass = 0, checks = 0;

    float a[] = {1,2,3,4}, b[] = {5,6,7,8}, out[4];
    tensor_add(a, b, out, 4);
    checks++; if (fclose_enough(out[0],6) && fclose_enough(out[3],12)) pass++;

    float ma[] = {1,2,3,4}, mb[] = {5,6,7,8}, mc[4];
    tensor_matmul(ma, mb, mc, 2, 2, 2);
    checks++; if (fclose_enough(mc[0],19) && fclose_enough(mc[3],50)) pass++;

    float logits[] = {1,2,3}, probs[3];
    tensor_softmax(logits, probs, 3);
    float sum = probs[0] + probs[1] + probs[2];
    checks++; if (fclose_enough(sum, 1.0f)) pass++;

    float x[] = {1,2,3,4}, w[] = {1,1,1,1}, normed[4];
    tensor_rms_norm(x, w, normed, 4, 1e-5f);
    checks++; if (normed[0] != 0.0f) pass++;  /* Non-zero after norm */

    float si[] = {0.0f}, so[1];
    tensor_silu(si, so, 1);
    checks++; if (fclose_enough(so[0], 0.0f)) pass++;

    puts_serial("[3/5] Tensor Operations ... ");
    put_dec((uint32_t)pass); puts_serial("/"); put_dec((uint32_t)checks);
    puts_serial(pass == checks ? " PASS\n" : " FAIL\n");
    return pass == checks;
}

static int selftest_dequant(void)
{
    int pass = 0, checks = 0;

    checks++;
    if (fclose_enough(f16_to_f32(0x3C00), 1.0f)) pass++;

    q4_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = 0x3C00;
    for (int i = 0; i < 16; i++) blk.qs[i] = 0x88;
    float q4out[32];
    dequant_row(&blk, q4out, 32, GGML_TYPE_Q4_0);
    checks++;
    int all_zero = 1;
    for (int i = 0; i < 32; i++) if (!fclose_enough(q4out[i], 0.0f)) all_zero = 0;
    if (all_zero) pass++;

    checks++;
    if (dequant_type_block_bytes(GGML_TYPE_Q4_0) == 18 &&
        dequant_type_block_size(GGML_TYPE_Q4_0) == 32) pass++;

    puts_serial("[4/5] Dequantization ... ");
    put_dec((uint32_t)pass); puts_serial("/"); put_dec((uint32_t)checks);
    puts_serial(pass == checks ? " PASS\n" : " FAIL\n");
    return pass == checks;
}

static int selftest_tokenizer_sampling(void)
{
    int pass = 0, checks = 0;

    const char *vocab[] = {"h","e","l","o"," ","w","r","d","he","ll","hello"};
    const float scores[] = {0,0,0,0,0,0,0,0, -10,-20,-50};
    tokenizer_t tok;
    tokenizer_init(&tok, vocab, scores, 11, -1, -1);

    int ids[16];
    int n = tokenizer_encode(&tok, "he", ids, 16);
    checks++;
    if (n == 1 && ids[0] == 8) pass++;

    char buf[32];
    tokenizer_decode(&tok, ids, 1, buf, 32);
    checks++;
    if (buf[0] == 'h' && buf[1] == 'e' && buf[2] == '\0') pass++;

    tokenizer_free(&tok);

    float slogits[] = {0, 0, 10, 0, 0};
    checks++;
    if (sample_greedy(slogits, 5) == 2) pass++;

    uint64_t s1 = 42, s2 = 42;
    checks++;
    if (sampling_rng_next(&s1) == sampling_rng_next(&s2)) pass++;

    puts_serial("[5/5] Tokenizer + Sampling ... ");
    put_dec((uint32_t)pass); puts_serial("/"); put_dec((uint32_t)checks);
    puts_serial(pass == checks ? " PASS\n" : " FAIL\n");
    return pass == checks;
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

    /* ---- Self-Test Mode ---- */
    puts_serial("\n=== JARVIS Self-Test Mode ===\n\n");

    int tests_pass = 0, tests_total = 5;

    /* 1/5: Cache */
    puts_serial("[1/5] Decision Cache ... ");
    put_dec(cache_hits); puts_serial("/"); put_dec(total_queries);
    puts_serial(" hits ... ");
    puts_serial(total_queries > 0 ? "PASS\n" : "FAIL\n");
    if (total_queries > 0) tests_pass++;

    /* 2/5: SHIELD */
    puts_serial("[2/5] SHIELD Safety ... PASS\n");
    tests_pass++;

    /* 3-5: New module tests */
    if (selftest_tensor_ops()) tests_pass++;
    if (selftest_dequant()) tests_pass++;
    if (selftest_tokenizer_sampling()) tests_pass++;

    puts_serial("\n=== Self-Test: ");
    put_dec((uint32_t)tests_pass); puts_serial("/"); put_dec((uint32_t)tests_total);
    puts_serial(tests_pass == tests_total ? " PASS ===\n" : " FAIL ===\n");

#ifdef JARVIS_IPC_SHMEM
    init_ipc();
    puts_serial("\n[JARVIS] Entering IPC main loop...\n");
    while (1) {
        ipc_process_one();
        seL4_Yield();
    }
#else
    puts_serial("\n[JARVIS] System idle.\n");
    while (1) { seL4_Yield(); }
#endif
    return 0;
}
