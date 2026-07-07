/*
 * JARVIS AI-OS Phase 3 — Inference Server Process (Process B)
 *
 * Separate seL4 process for LLM inference. Spawned by the rootserver
 * (Process A) via sel4utils_configure_process + CPIO archive.
 *
 * Communication: Two shared memory rings (request A→B, response B→A)
 * mapped into both VSpaces. seL4_Notification caps for signaling.
 *
 * argv[0] = request notification cap (Process A signals when request ready)
 * argv[1] = response notification cap (Process B signals when response ready)
 * argv[2] = shared memory vaddr (two shmem_ring_t pages, request then response)
 * argv[3] = model vaddr (GRUB module mapped by Process A, 0 if none)
 * argv[4] = model size in bytes (0 if no GRUB module)
 */

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>

#include "shmem_ipc.h"
#include "jarvis_debug.h"

#if JARVIS_AVX2_PROBE
#include "avx2_probe.h"
#endif

#if JARVIS_SMP_PROBE
#include "smp_probe.h"
#endif

/* RDTSC helper — UNCONDITIONAL since v4: the generation loop is always timed for the live
 * tok/s measurement (MSG_INFER_STATS). Previously gated under JARVIS_M1_MEASURE. */
static inline uint64_t m1_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#include "gguf_parser.h"
#include "llama_model.h"
#include "llama_quant.h"
#include "gguf_vocab.h"
#include "tokenizer.h"
#include "sampling.h"
#include "threadpool.h"
#include "shared_context.h"
#if JARVIS_G3_RETRIEVAL
#include "g3_retrieval.h"       /* G3/M2: budget helper + macros (flag-gated; OFF pulls in nothing new) */
#endif

#ifdef JARVIS_HAS_MODEL
extern const unsigned char _binary_model_gguf_start[];
extern const unsigned char _binary_model_gguf_end[];
#endif

#if JARVIS_RESPAWN
/* ===== Phase 6 reuse-in-place respawn PRIMITIVES (SYSTEM_DESIGN §4.1/§4.2). Shared by the
 * K/M2a-2/b-1 measurement spike (JARVIS_KM2A_SPIKE) AND the K/M2b-2 live self-heal (JARVIS_ACTIONS)
 * — gated JARVIS_RESPAWN = (KM2A_SPIKE || ACTIONS). Deploy with both 0 = action-inert. ===== */
#ifndef JARVIS_MAX_WORKERS
#define JARVIS_MAX_WORKERS 8
#endif
#define KM2A_RESTART_STACK_SIZE (256 * 1024)   /* >= PB-main guarded-stack budget; keep in sync with main_x86.c */
/* External linkage so Process A can resolve its vaddr from PB's .symtab (resolve_pb_symbol). */
_Alignas(64) unsigned char g_km2a_restart_stack[KM2A_RESTART_STACK_SIZE];
/* Warm-context handles stashed after PB setup. Pointers into main()'s frame stay valid because
 * the restart re-enters on g_km2a_restart_stack (fresh SP), preserving main()'s frame; §4.2 item
 * G's true file-scope-static hoist lands in K/M2b when this code becomes permanent. */
static shmem_ring_t  *g_km2a_req_ring, *g_km2a_resp_ring;
static seL4_CPtr      g_km2a_req_notif, g_km2a_resp_notif;
static qmodel_t      *g_km2a_qm;
static llama_state_t *g_km2a_state;
static tokenizer_t   *g_km2a_tok;
static int            g_km2a_bos_id, g_km2a_model_loaded;
static int            g_km2a_pool_n_threads = 1, g_km2a_pool_n_wake = 0;
static seL4_CPtr      g_km2a_pool_done = 0, g_km2a_pool_wake[JARVIS_MAX_WORKERS];
static uint32_t       g_km2a_restart_count = 0;
#endif
#if JARVIS_KM2A_SPIKE
static volatile int   g_km2a_please_restart = 0;   /* E: PA-requested cooperative restart (spike-only) */
#endif

#if JARVIS_ACTION_PROBE && defined(JARVIS_SEL4_SMP)
/* STEP-3 Part-2: worker-fault probe fn. The first pool thread whose stack anchor is FAR from the
 * dispatcher's takes the one-shot null-READ — workers run on their own sel4utils stacks while the
 * dispatcher (PB-main) runs wfault_fn on the stack that set g_wfault_anchor, so only a genuine
 * WORKER can fault (a deterministic worker VMFault, never PB-main). The pause spin keeps indexes
 * flowing long enough (~0.5 ms total) that the workers' wake latency (~µs) always joins the race. */
static volatile int       g_wfault_arm = 0;
static volatile uintptr_t g_wfault_anchor = 0;
static void wfault_fn(int i, void *ctx)
{
    (void)i; (void)ctx;
    char here;
    uintptr_t h = (uintptr_t)&here, a = g_wfault_anchor;
    uintptr_t d = h > a ? h - a : a - h;
    if (g_wfault_arm && d > (256UL * 1024UL)) {   /* not the dispatcher's stack -> a worker */
        g_wfault_arm = 0;
        volatile int *nullp = (volatile int *)0;
        volatile int v = *nullp; (void)v;          /* -> worker VMFault -> badged fault EP -> PA */
    }
    for (volatile int s = 0; s < 400; s++) __asm__ volatile("pause");
}
#endif

/* G (Phase 6 K/M2b-2): the warm inference state hoisted to FILE-SCOPE STATICS (was main()
 * locals). Deletes the pointer-into-frame fragility — pb_restart_entry / the K/M2a-2 stash now
 * reference fixed .bss objects, never a main()-frame pointer. UNGATED + behavior-neutral: the
 * structs move stack->.bss (the ~40 MiB KV / fwd_scratch live behind pointers), generation is
 * deterministic so OFF [INFER] is byte-identical. main() aliases the historical local names via
 * #define (scoped to main() only, #undef'd at its end) so handle_query's `&qm->config` PARAM
 * sites (before main) are untouched. */
static qmodel_t      g_pbm_qm;
static gguf_vocab_t  g_pbm_vocab;
static tokenizer_t   g_pbm_tok;
static llama_state_t g_pbm_state;

/* ---- Serial output (via seL4 debug syscall, same as rootserver) ---- */

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
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (--i >= 0) seL4_DebugPutChar(buf[i]);
}

/* ---- Debug log helper: serial + IPC to Process A for NVMe logging ---- */

static shmem_ring_t *g_resp_ring = NULL;  /* set in main(), used by pb_log */
/* Phase 5 G2/M3: the live context pool (mapped frame 3), set once in main() at the M1 derive.
 * handle_query reads it per inference (READ-ONLY — G2 never injects; injection is G3). */
static shared_context_t *g_sctx_pb = NULL;

/* Check if response ring has room for debug messages.
 * Reserve at least 3 slots for MSG_RESPONSE chunks.
 * Returns 1 if safe to log, 0 if ring is getting full. */
__attribute__((unused)) static int pb_can_log(void)
{
    if (!g_resp_ring) return 0;
    uint32_t wr = __atomic_load_n(&g_resp_ring->header.write_idx, __ATOMIC_RELAXED);
    uint32_t rd = __atomic_load_n(&g_resp_ring->header.read_idx, __ATOMIC_ACQUIRE);
    return (wr - rd) < (g_resp_ring->header.size - 3);
}

static void pb_log(const char *msg)
{
    puts_serial(msg);
    puts_serial("\n");
#if JARVIS_DBG_PB
    if (g_resp_ring && pb_can_log()) {
        int len = 0;
        const char *p = msg;
        while (*p++) len++;
        shmem_ipc_send(g_resp_ring, MSG_DEBUG, 0,
                       msg, (uint16_t)(len > 240 ? 240 : len));
    }
#endif
}

/* Build a debug line with a number suffix: e.g. "[PB] Prefill token 4/15" */
static void pb_log_num(const char *prefix, uint32_t val, const char *suffix)
{
    char buf[128];
    int pos = 0;
    const char *s = prefix;
    while (*s && pos < 110) buf[pos++] = *s++;
    /* decimal */
    char d[12]; int di = 0;
    if (val == 0) d[di++] = '0';
    else { uint32_t v = val; while (v > 0) { d[di++] = '0' + (v % 10); v /= 10; } }
    while (--di >= 0 && pos < 120) buf[pos++] = d[di];
    s = suffix;
    while (*s && pos < 126) buf[pos++] = *s++;
    buf[pos] = '\0';
    pb_log(buf);
}

/* ---- Process incoming IPC requests ---- */

static void handle_query(shmem_ring_t *response_ring, seL4_CPtr resp_notif,
                          uint16_t seq, const char *query, uint16_t query_len,
                          qmodel_t *qm, llama_state_t *state, tokenizer_t *tok,
                          int bos_id)
{
    /* Phase 5 G2/M3: read the LIVE context pool per inference, from the mapped page, small-stack
     * only (the <8 KB PB-stack rule — never copy the 4 KB pool / rings onto the stack). This is the
     * exact cross-process read path G3 (retrieval) will use. READ-ONLY: `ps`/`recent` are NOT used to
     * change the prompt, tokens, or any downstream state — G2 never injects (that is G3), so generation
     * stays byte-identical. A one-shot [SCTX-PB] proof shows PB read PA's LIVE writes (q_total>0/seq>2). */
    if (g_sctx_pb) {
        sctx_system_state_t ps;
        int retries = sctx_read_state(g_sctx_pb, &ps);      /* seqlock snapshot-retry (~64 B stack) */
        sctx_decision_t recent[4];
        int nrec = sctx_recent(g_sctx_pb, 4, recent, 4);    /* tiny ring read (~128 B) — G3's read path */
        static int sctx_pb_logged = 0;
        if (!sctx_pb_logged) {
            sctx_pb_logged = 1;
            pb_log_num("[SCTX-PB] live read q_total=", (uint32_t)ps.q_total, "");
            pb_log_num("[SCTX-PB]   seq=",     __atomic_load_n(&g_sctx_pb->seq, __ATOMIC_ACQUIRE), "");
            pb_log_num("[SCTX-PB]   recent=",  (uint32_t)nrec, "");
            pb_log_num("[SCTX-PB]   retries=", (uint32_t)retries, "");
        }
    }

    /* Build Gemma 4 chat template prompt with direct control token IDs:
     *   <bos> <|turn> user \n {query} <turn|> \n <|turn> model \n <|think|>
     * Tokens: bos=2, <|turn>=105, user=2364, \n=107, <turn|>=106, model=4368, <|think|>=98
     * Stop on <eos>=1, NOT eos_id=106 (that's <turn|> which model emits first). */
    int prompt_ids[256];          /* G3/M2: was [128]; room for preamble+query. KV stays 512. */
    int n_prompt = 0;
    prompt_ids[n_prompt++] = bos_id;        /* <bos> */
    prompt_ids[n_prompt++] = 105;           /* <|turn> */
    prompt_ids[n_prompt++] = 2364;          /* user */
    prompt_ids[n_prompt++] = 107;           /* \n */

#if JARVIS_G3_RETRIEVAL
    /* G3/M2: inject the PA-packed retrieval preamble inside the user turn, AFTER the \n and
     * BEFORE the question. PB tokenizes the assembled text blob (PA has no tokenizer). Bounded
     * by g3_prompt_budget so the query is never starved and prompt_ids never overflows. Flag
     * OFF compiles this out -> generation byte-identical. */
    if (g_sctx_pb) {
        char pre_buf[512];   /* g3 caps the preamble at 512 B; keeps handle_query stack < 8 KB */
        uint32_t pre_len = sctx_get_preamble(g_sctx_pb, pre_buf, sizeof(pre_buf));
        int n_pre = 0;
        if (pre_len > 0) {
            int budget = g3_prompt_budget(n_prompt,
                             (int)(sizeof(prompt_ids) / sizeof(prompt_ids[0])),
                             G3_QUERY_FLOOR_TOKS, G3_SUFFIX_TOKS);
            if (budget > 0) {
                n_pre = tokenizer_encode(tok, pre_buf, prompt_ids + n_prompt, budget);
                if (n_pre > 0) n_prompt += n_pre;
            }
        }
        /* one-shot injection proof — mirrors [SCTX-PB] so it shows in the box smoke log without
         * JARVIS_DBG_PB (the full token dump at the [PB] tokens line still needs DBG_PB for M3). */
        static int retr_pb_logged = 0;
        if (!retr_pb_logged) {
            retr_pb_logged = 1;
            pb_log_num("[RETR-PB] preamble_len=",  pre_len, "");
            pb_log_num("[RETR-PB] preamble_toks=", (uint32_t)(n_pre > 0 ? n_pre : 0), "");
        }
    }
#endif

    /* Null-terminate the query */
    char query_buf[241];
    int qlen = query_len > 240 ? 240 : query_len;
    memcpy(query_buf, query, (size_t)qlen);
    query_buf[qlen] = '\0';

#if JARVIS_ACTION_PROBE
    /* K/M2b-2 STEP-3 real-crash probe: a GENUINE PB-main VMFault (deliberate null-READ — NEVER a
     * wild write, so PB's .data the respawn preserves is intact) so PA's fault-EP receiver + the
     * register-rewrite respawn are exercised on a REAL fault. Gated; deploy never sends the marker. */
    if (qlen == 10 && memcmp(query_buf, "FAULTPROBE", 10) == 0) {
        puts_serial("[PB] ACTION_PROBE: inducing PB-main null-READ fault NOW\n");
        volatile int *nullp = (volatile int *)0;
        volatile int v = *nullp; (void)v;   /* -> VMFault -> PB fault EP -> PA */
    }
    /* K/M2c HANG probe: PB-main WEDGES — it NEVER returns to pb_serve_loop, so it ACKs nothing AND
     * never faults (the fault EP stays SILENT). This is the "alive but wedged" failure the crash
     * lane can't see; PA detects it via the consecutive-miss counter. The wedge YIELDS (not a
     * `pause` busy-loop): PA and PB-main share core 0 (only the M3 workers are pinned to cores
     * 1-5), so a non-yielding spin would STARVE PA on core 0 and it could never run the detection
     * loop — a crash works only because the fault deschedules PB-main. `for(;;) seL4_Yield()`
     * models a real soft-hang (alive, scheduled, but stuck not serving) and lets PA run.
     * km2b_do_respawn's Suspend-first deschedules it and the register-rewrite recovers.
     * "HANGPROBE" is 9 chars. Gated; the deployed workload never sends the marker. */
    if (qlen == 9 && memcmp(query_buf, "HANGPROBE", 9) == 0) {
        puts_serial("[PB] ACTION_PROBE: WEDGING PB-main (yield-loop, no ACK, no fault)\n");
        for (;;) seL4_Yield();   /* PA (core 0) gets scheduled -> miss-counter climbs -> "hang" restart */
    }
    /* K/M4 pre-flip experiment (§10 carry-forward #1): the HARD (non-yielding) counterpart to the
     * HANGPROBE soft yield-loop. Settles whether the PRODUCTION polling path detects a PB-main that
     * NEVER cooperatively deschedules. RESULT (2026-07-07): OUTCOME B — a hard loop is NOT detected.
     * The timer tick does NOT rotate core 0 to PA against a non-yielding PB (PA's production wait
     * YIELDS the core to PB each iteration -> PA STARVES); only deschedule-able wedges (soft-yield,
     * crash-fault) are caught. See §4.2. KVM ONLY — an Outcome-B starve just hangs QEMU (pkill-
     * recoverable), never a bare-metal brick. "HARDLOOP"=8 chars. */
    if (qlen == 8 && memcmp(query_buf, "HARDLOOP", 8) == 0) {
        puts_serial("[PB] ACTION_PROBE: HARD non-yielding loop (no yield/ACK/fault)\n");
        for (;;) { }   /* constant controlling expr => a well-defined infinite loop; never deschedules */
    }
    /* PAUSELOOP: identical wedge with a `pause` hint — scheduling-identical to the bare loop, it only
     * rules out a compiler-optimized empty-loop artifact. Run ONLY after HARDLOOP already recovered. */
    if (qlen == 9 && memcmp(query_buf, "PAUSELOOP", 9) == 0) {
        puts_serial("[PB] ACTION_PROBE: pause loop (no yield/ACK/fault)\n");
        for (;;) __asm__ volatile ("pause");
    }
#ifdef JARVIS_SEL4_SMP
    /* STEP-3 Part-2: the WORKER-fault probe (the corrected §10 gate needs a real fault in PB-main
     * AND a worker). Dispatch a poisoned pool run: the first thread whose stack anchor is far from
     * the dispatcher's (workers run on their own sel4utils stacks) takes a one-shot null-READ ->
     * genuine worker VMFault -> PA's badged fault EP attributes it (badge=i). The faulted worker
     * never decrements `active`, so this dispatch's join blocks PB-main in seL4_Wait(done) — BY
     * DESIGN: PA's whole-PB restart (Suspend-first) is exactly what recovers from it. */
    if (qlen == 11 && memcmp(query_buf, "WFAULTPROBE", 11) == 0) {
        char anchor;
        g_wfault_anchor = (uintptr_t)&anchor;
        g_wfault_arm = 1;
        puts_serial("[PB] ACTION_PROBE: dispatching poisoned pool run (worker null-READ)\n");
        jarvis_parallel_for(0, 512, wfault_fn, NULL, 0);
        /* Reached only if NO thread took the poison (dispatcher consumed every index before any
         * worker woke — never observed in practice). Respond so PA's probe reports honestly. */
        g_wfault_arm = 0;
        puts_serial("[PB] ACTION_PROBE: worker-fault probe NOT taken (dispatcher won the race)\n");
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq, "WFPROBE-MISS", 12);
        seL4_Signal(resp_notif);
        return;
    }
#endif
#endif

#if JARVIS_DBG_PB
    {
        char hq[280] = "[PB] handle_query: \"";
        int hi = 20;
        const char *hp = query_buf;
        while (*hp && hi < 270) hq[hi++] = *hp++;
        hq[hi++] = '"'; hq[hi] = '\0';
        pb_log(hq);
    }
#endif

#if JARVIS_KM2A_SPIKE
    /* K/M2b-2 probe markers (gated; the deployed workload never sends them). */
    if (qlen == 7 && memcmp(query_buf, "TOKSPIN", 7) == 0) {
        /* mode-2 (mid-TOKENIZE): linger AT the tokenize phase so PA can async-Suspend PB here — the
         * malloc window K/M2b-1's mid-generation restart couldn't reach. With the alloc-free
         * tokenizer (E), this phase does NO malloc, so a restart fired here has no arena to tear. */
        uint64_t _t0 = m1_rdtsc();
        while (m1_rdtsc() - _t0 < 2000000000ULL) __asm__ volatile("pause");   /* ~540 ms @ 3.7 GHz */
    } else if (qlen == 4 && memcmp(query_buf, "COOP", 4) == 0) {
        /* E cooperative flag: request a self-driven restart at the next pb_serve_loop top (a
         * quiescent point) instead of PA async-Suspending. Ack + return; the loop-top check fires. */
        g_km2a_please_restart = 1;
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq, "", 0);
        seL4_Signal(resp_notif);
        return;
    }
#endif

    /* Encode user text */
    n_prompt += tokenizer_encode(tok, query_buf, prompt_ids + n_prompt,
                                  (int)(sizeof(prompt_ids) / sizeof(prompt_ids[0])) - n_prompt - 6);

    /* Close user turn + open model turn + think */
    prompt_ids[n_prompt++] = 106;           /* <turn|> */
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 105;           /* <|turn> */
    prompt_ids[n_prompt++] = 4368;          /* model */
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 98;            /* <|think|> */

#if JARVIS_DBG_PB
    /* DEBUG: print token IDs for comparison against llama.cpp reference */
    printf("[PB] tokens (%d):", n_prompt);
    for (int i = 0; i < n_prompt && i < 20; i++)
        printf(" %d", prompt_ids[i]);
    printf("\n");
#endif

    /* Reset state for new generation */
    state->pos = 0;
    size_t kv_bytes = (size_t)state->kv_n_layers * state->max_seq_len *
                      qm->config.n_kv_heads * qm->config.head_dim * sizeof(float);
    memset(state->key_cache, 0, kv_bytes);
    memset(state->value_cache, 0, kv_bytes);
#if JARVIS_DBG_PB
    pb_log("[PB] KV reset done");
    pb_log_num("[PB] Tokenized: ", (uint32_t)n_prompt, " tokens");
    pb_log("[PB] Starting prefill...");
#endif

    /* Reset SSM state if applicable */
    if (state->conv_state && state->n_deltanet > 0) {
        const llama_config_t *cfg = &qm->config;
        int qkv_dim = cfg->ssm_n_group * cfg->ssm_d_state * 2 + cfg->ssm_d_inner;
        size_t conv_bytes = (size_t)state->n_deltanet * (cfg->ssm_d_conv - 1) *
                            qkv_dim * sizeof(float);
        memset(state->conv_state, 0, conv_bytes);
    }
    if (state->ssm_state && state->n_deltanet > 0) {
        const llama_config_t *cfg = &qm->config;
        int head_v_dim = cfg->ssm_d_inner / cfg->ssm_dt_rank;
        size_t ssm_bytes = (size_t)state->n_deltanet * cfg->ssm_dt_rank *
                           cfg->ssm_d_state * head_v_dim * sizeof(float);
        memset(state->ssm_state, 0, ssm_bytes);
    }

    for (int i = 0; i < n_prompt; i++) {
#if JARVIS_DBG_PB
        if (i == 0 || i == n_prompt - 1 || (i % 4 == 0)) {
            char pfb[48];
            int pfi = 0;
            const char *pfs = "[PB] Prefill token ";
            while (*pfs) pfb[pfi++] = *pfs++;
            /* i */
            { char d[10]; int di=0; uint32_t v=(uint32_t)i;
              if(v==0) d[di++]='0'; else while(v>0){d[di++]='0'+(v%10);v/=10;}
              while(--di>=0) pfb[pfi++]=d[di]; }
            pfb[pfi++] = '/';
            /* n_prompt */
            { char d[10]; int di=0; uint32_t v=(uint32_t)n_prompt;
              if(v==0) d[di++]='0'; else while(v>0){d[di++]='0'+(v%10);v/=10;}
              while(--di>=0) pfb[pfi++]=d[di]; }
            pfb[pfi] = '\0';
            pb_log(pfb);
        }
#endif
        qmodel_forward(qm, state, prompt_ids[i]);
    }
#if JARVIS_DBG_PB
    pb_log("[PB] Prefill complete, generating...");
#endif

    /* Autoregressive generation with per-token logging */
    int output_ids[64];
    int n_gen = 0;
    /* v4 live tok/s: the generation loop is ALWAYS timed (two RDTSC reads — immaterial vs the
     * ~seconds-long loop). Raw tokens+cycles go to PA via MSG_INFER_STATS; PA converts to tok/s.
     * The gated M1_MEASURE MSG_DEBUG report below is unchanged (offline-conversion text form). */
    uint64_t gen_t0 = m1_rdtsc();
    while (n_gen < 50 && state->pos < state->max_seq_len) {
        int next = sample_greedy(state->logits, qm->config.vocab_size);
        output_ids[n_gen++] = next;
#if JARVIS_DBG_PB
        {
            char gb[64] = "[PB] Generated token ";
            int gi = 21;
            { char d[10]; int di=0; uint32_t v=(uint32_t)n_gen;
              if(v==0) d[di++]='0'; else while(v>0){d[di++]='0'+(v%10);v/=10;}
              while(--di>=0) gb[gi++]=d[di]; }
            const char *ids = ": id=";
            while (*ids) gb[gi++] = *ids++;
            { char d[10]; int di=0; uint32_t v=(uint32_t)next;
              if(v==0) d[di++]='0'; else while(v>0){d[di++]='0'+(v%10);v/=10;}
              while(--di>=0) gb[gi++]=d[di]; }
            gb[gi] = '\0';
            pb_log(gb);
        }
#endif
        if (next == 1 /* <eos> */) break;
        qmodel_forward(qm, state, next);
    }
#if JARVIS_DBG_PB
    pb_log_num("[PB] Generation complete: ", (uint32_t)n_gen, " tokens");
#endif
    uint64_t gen_cyc = m1_rdtsc() - gen_t0;
#if JARVIS_M1_MEASURE
    uint64_t m1_cyc = gen_cyc;
#endif

    /* v4 live tok/s: report the RAW measurement to PA BEFORE the response chunks, so PA's
     * response drain latches it in the same pass (ring order: stats, then chunks — race-free).
     * Best-effort: if the ring is unexpectedly full the stats are skipped, never the response. */
    {
        infer_stats_msg_t st;
        st.tokens = (uint32_t)n_gen;
        st.tsc_cycles = gen_cyc;
        shmem_ipc_send(response_ring, MSG_INFER_STATS, seq, &st, (uint16_t)sizeof st);
    }

    /* Decode to text */
    char text_out[512];
    int text_len = tokenizer_decode(tok, output_ids, n_gen, text_out, sizeof(text_out));
    if (text_len < 0) text_len = 0;
#if JARVIS_DBG_PB
    pb_log_num("[PB] decoded ", (uint32_t)text_len, " bytes");
#endif

    /* Send response — split into multiple messages if >240 bytes */
    int offset = 0;
    uint16_t msg_seq = seq;
#if JARVIS_DBG_PB
    puts_serial("[PB] send loop start\n");
#endif
#if JARVIS_DBG_RING
    /* Ring health check */
    puts_serial("[PB] ring @"); put_dec((uint32_t)(uintptr_t)response_ring);
    puts_serial(" magic="); put_dec(response_ring->header.magic);
    puts_serial(" w="); put_dec(response_ring->header.write_idx);
    puts_serial(" r="); put_dec(response_ring->header.read_idx);
    puts_serial("\n");
#endif
    while (offset < text_len) {
        int chunk = text_len - offset;
        if (chunk > SHMEM_MAX_PAYLOAD) chunk = SHMEM_MAX_PAYLOAD;
#if JARVIS_DBG_RING
        puts_serial("[PB] chunk @"); put_dec((uint32_t)offset);
        puts_serial(" len="); put_dec((uint32_t)chunk); puts_serial("\n");
#endif
        int rc = shmem_ipc_send(response_ring, MSG_RESPONSE, msg_seq,
                       text_out + offset, (uint16_t)chunk);
#if JARVIS_DBG_RING
        puts_serial("[PB] send rc="); put_dec((uint32_t)rc); puts_serial("\n");
#else
        (void)rc;
#endif
        offset += chunk;
        msg_seq++;
    }

    /* If empty response, send empty message */
    if (text_len == 0) {
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq, "", 0);
    }

#if JARVIS_M1_MEASURE
    /* M1: report decode timing + a printable response snippet to PA for the NVMe log
     * (LOG_INFER). Offline: tok/s = gen * TSC_HZ / cyc, TSC_HZ = 3.7e9 on the 2700X.
     * The snippet lets generation coherence be verified from the NVMe log alone (no
     * serial capture needed). Capped well under SHMEM_MAX_PAYLOAD (240). */
    {
        char m1[240]; int p = 0;
        const char *pre = "M1 gen=";
        while (*pre) m1[p++] = *pre++;
        { char d[12]; int di = 0; uint32_t v = (uint32_t)n_gen;
          if (v == 0) d[di++] = '0'; else while (v) { d[di++] = (char)('0' + v % 10); v /= 10; }
          while (--di >= 0) m1[p++] = d[di]; }
        const char *cc = " cyc=";
        while (*cc) m1[p++] = *cc++;
        { char d[24]; int di = 0; uint64_t v = m1_cyc;
          if (v == 0) d[di++] = '0'; else while (v) { d[di++] = (char)('0' + v % 10); v /= 10; }
          while (--di >= 0) m1[p++] = d[di]; }
        const char *sep = " | ";
        while (*sep) m1[p++] = *sep++;
        for (int i = 0; i < text_len && p < 236; i++) {
            char c = text_out[i];
            m1[p++] = (c >= 0x20 && c <= 0x7e) ? c : '.';  /* printable-only for the log */
        }
        m1[p] = '\0';
        shmem_ipc_send(response_ring, MSG_DEBUG, 0, m1, (uint16_t)p);
    }
#endif

#if JARVIS_DBG_PB
    puts_serial("[PB] signaling Process A\n");
#endif
    seL4_Signal(resp_notif);
#if JARVIS_DBG_PB
    puts_serial("[PB] response sent\n");
#endif
}

/* ---- IPC serve loop (extracted so both main() and the K/M2a-2 pb_restart_entry drive one
 *      source; SYSTEM_DESIGN §4.2 item G). Behavior-neutral vs the prior inline loop —
 *      generation is deterministic, so [INFER] stays byte-identical when JARVIS_KM2A_SPIKE=0. ---- */
static void pb_serve_loop(shmem_ring_t *request_ring, shmem_ring_t *response_ring,
                          seL4_CPtr req_notif, seL4_CPtr resp_notif,
                          qmodel_t *qm, llama_state_t *state, tokenizer_t *tok,
                          int bos_id, int model_loaded)
{
    uint32_t pb_query_count = 0;
    while (1) {
        pb_query_count++;
        pb_log_num("[PB] Waiting for query #", pb_query_count, "");

#if JARVIS_KM2A_SPIKE
        /* E cooperative restart: at this quiescent loop top (workers parked, no malloc in flight),
         * self-drive the reset PA requested via the flag — drain the pool wake/done pending-bits,
         * re-init the pool, re-signal ready — instead of PA async-Suspend-ing a busy PB. */
        if (g_km2a_please_restart) {
            g_km2a_please_restart = 0;
            puts_serial("[RESTART-COOP] self-restart at loop top\n");
#ifdef JARVIS_SEL4_SMP
            for (int i = 1; i < g_km2a_pool_n_threads && i < JARVIS_MAX_WORKERS; i++)
                if (g_km2a_pool_wake[i]) { seL4_Word bw; seL4_Poll(g_km2a_pool_wake[i], &bw); }
            if (g_km2a_pool_done) { seL4_Word bd; seL4_Poll(g_km2a_pool_done, &bd); }
            jarvis_sel4_pool_init(g_km2a_pool_n_threads, g_km2a_pool_done,
                                  g_km2a_pool_wake + 1, g_km2a_pool_n_wake);
#endif
            shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
            seL4_Signal(resp_notif);
            continue;
        }
#endif

#if JARVIS_AVX2_PROBE
        /* M0 AVX2-under-preemption gate: a burst of long YMM reductions each loop
         * cycle, interleaved with the live PA<->PB workload (timer ticks + IPC
         * force context switches; PA also dirties YMM via avx2_probe_touch). */
        for (int _pi = 0; _pi < 8; _pi++)
            avx2_probe_run("PB", ((uint64_t)pb_query_count << 8) ^ (uint64_t)_pi ^ 0x5A5Au);
#endif

        /* Wait for signal from Process A */
        seL4_Wait(req_notif, NULL);

        pb_log_num("[PB] Woke for query #", pb_query_count, "");

        /* Process all pending requests */
        uint8_t msg_type;
        uint16_t msg_seq;
        uint8_t payload[SHMEM_MAX_PAYLOAD];
        uint16_t msg_len;

        while (shmem_ipc_recv(request_ring, &msg_type, &msg_seq, payload, &msg_len) == 0) {
            switch (msg_type) {
            case MSG_QUERY:
                if (!model_loaded) {
                    /* No model — send empty response */
                    shmem_ipc_send(response_ring, MSG_RESPONSE, msg_seq, "", 0);
                    seL4_Signal(resp_notif);
                    break;
                }
                handle_query(response_ring, resp_notif,
                             msg_seq, (const char *)payload, msg_len,
                             qm, state, tok, bos_id);
                break;

            case MSG_HEARTBEAT:
                shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, msg_seq, NULL, 0);
                seL4_Signal(resp_notif);
                break;

            case MSG_SHIELD_CHECK: {
                /* For now, just echo back ALLOW — full model-assisted SHIELD is future work */
                uint8_t result = 0; /* SHIELD_ALLOW */
                shmem_ipc_send(response_ring, MSG_SHIELD_RESULT, msg_seq, &result, 1);
                seL4_Signal(resp_notif);
                break;
            }

            default:
                /* Unknown message type — ignore */
                break;
            }
        }
    }
}

#if JARVIS_RESPAWN
/* K/M2a-2 (SYSTEM_DESIGN §4.2): PA suspends PB-main at a quiescent point (parked on
 * seL4_Wait(req_notif)) and WriteRegisters it HERE with a fresh SP = g_km2a_restart_stack top
 * (rsp ≡ 8 mod 16), fs_base preserved, DF clear, then Resumes. This re-enters PAST musl's
 * one-time per-process init (the naive spawn_process_v re-call aborts there, §4.1) and REUSES the
 * warm qm/state/tok (no re-alloc — the ~40 MiB KV stays put). External linkage + used/noinline so
 * it survives -O2/--gc-sections and resolve_pb_symbol finds it. */
static void km2a_putdec(uint32_t v) {
    char d[12]; int di = 0;
    if (v == 0) d[di++] = '0'; else while (v) { d[di++] = (char)('0' + v % 10); v /= 10; }
    char out[13]; int oi = 0;
    while (di) out[oi++] = d[--di];
    out[oi] = '\0';
    puts_serial(out);
}
static void km2a_puthex64(const char *label, uint64_t v) {
    static const char hx[] = "0123456789abcdef";
    char b[40]; int p = 0;
    while (*label) b[p++] = *label++;
    b[p++] = '0'; b[p++] = 'x';
    for (int s = 60; s >= 0; s -= 4) b[p++] = hx[(v >> s) & 0xF];
    b[p++] = '\n'; b[p] = '\0';
    puts_serial(b);
}
static uint32_t km2a_stack_highwater(void) {
    uint32_t i = 0;
    while (i < KM2A_RESTART_STACK_SIZE && g_km2a_restart_stack[i] == 0xAA) i++;
    return KM2A_RESTART_STACK_SIZE - i;   /* bytes used (the stack grows down from the top) */
}
__attribute__((used, noinline)) _Noreturn void pb_restart_entry(void);
void pb_restart_entry(void)
{
    g_km2a_restart_count++;
#ifdef JARVIS_SEL4_SMP
    /* §4.2 F: drain the pool wake/done pending-bits via PB's own caps (PA cannot reach these
     * PB-CSpace caps), then reset the pool struct fields (jarvis_sel4_pool_init zeros
     * gen/next_idx/active — the parked workers are reused, not recreated: §4.2 B). */
    for (int i = 1; i < g_km2a_pool_n_threads && i < JARVIS_MAX_WORKERS; i++)
        if (g_km2a_pool_wake[i]) { seL4_Word bw; seL4_Poll(g_km2a_pool_wake[i], &bw); }
    if (g_km2a_pool_done) { seL4_Word bd; seL4_Poll(g_km2a_pool_done, &bd); }
    jarvis_sel4_pool_init(g_km2a_pool_n_threads, g_km2a_pool_done,
                          g_km2a_pool_wake + 1, g_km2a_pool_n_wake);
#endif
    /* PB-heap axis (§4.2 STEP-3): these warm-state heap pointers must be UNCHANGED vs the armed
     * baseline (any re-alloc moves them); the restart-stack high-water must keep headroom. */
    puts_serial("[RESTART-PB] cycle="); km2a_putdec(g_km2a_restart_count); puts_serial("\n");
    km2a_puthex64("[RESTART-PB]   key_cache=",   (uint64_t)(uintptr_t)g_km2a_state->key_cache);
    km2a_puthex64("[RESTART-PB]   value_cache=", (uint64_t)(uintptr_t)g_km2a_state->value_cache);
    km2a_puthex64("[RESTART-PB]   logits=",      (uint64_t)(uintptr_t)g_km2a_state->logits);
    km2a_puthex64("[RESTART-PB]   layers=",      (uint64_t)(uintptr_t)g_km2a_qm->layers);
    puts_serial("[RESTART-PB]   stack_hw="); km2a_putdec(km2a_stack_highwater());
    puts_serial(" of "); km2a_putdec(KM2A_RESTART_STACK_SIZE); puts_serial("\n");
    if (g_km2a_restart_stack[0] != 0xAA || g_km2a_restart_stack[63] != 0xAA)
        puts_serial("[RESTART-PB]   *** STACK CANARY CORRUPT (overflow) ***\n");
    /* re-signal ready — PA drained resp_notif then polls the resp ring for this ACK (§6 step 9) */
    shmem_ipc_send(g_km2a_resp_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
    seL4_Signal(g_km2a_resp_notif);
    /* re-enter the serve loop, reusing the warm state (no re-alloc) */
    pb_serve_loop(g_km2a_req_ring, g_km2a_resp_ring, g_km2a_req_notif, g_km2a_resp_notif,
                  g_km2a_qm, g_km2a_state, g_km2a_tok, g_km2a_bos_id, g_km2a_model_loaded);
    for (;;) seL4_Yield();   /* _Noreturn trap tail — pb_serve_loop never returns */
}
#endif

/* ---- Main ---- */

int main(int argc, char **argv)
{
/* G: alias the historical local names to the file-scope statics for main()'s body ONLY.
 * #undef'd before return. Token-based, so sctx_system_state_t / tokenizer_free are untouched. */
#define qm    g_pbm_qm
#define vocab g_pbm_vocab
#define tok   g_pbm_tok
#define state g_pbm_state
    puts_serial("[Process B] Inference server started\n");

    if (argc < 3) {
        puts_serial("[Process B] ERROR: expected 3 args (req_notif, resp_notif, shmem_vaddr)\n");
        puts_serial("[Process B] Got argc="); put_dec((uint32_t)argc); puts_serial("\n");
        /* Fall through to idle — don't crash */
        goto idle;
    }

    /* Parse arguments from rootserver */
    seL4_CPtr req_notif  = (seL4_CPtr)atol(argv[0]);
    seL4_CPtr resp_notif = (seL4_CPtr)atol(argv[1]);
    uintptr_t shmem_vaddr = (uintptr_t)atol(argv[2]);

    puts_serial("[Process B] req_notif="); put_dec((uint32_t)req_notif);
    puts_serial(" resp_notif="); put_dec((uint32_t)resp_notif);
    puts_serial("\n");

    /* Validate shared memory IPC rings (pre-initialized by Process A).
     * Do NOT call shmem_ipc_init() here — that would wipe any pending
     * messages from Process A (race condition). */
    shmem_ring_t *request_ring  = (shmem_ring_t *)shmem_vaddr;
    shmem_ring_t *response_ring = (shmem_ring_t *)(shmem_vaddr + SHMEM_PAGE_SIZE);
    /* Phase 5 G2/M1: the shared context pool is the 3rd frame (after the 2 IPC rings). */
    shared_context_t *sctx = (shared_context_t *)(shmem_vaddr + 2 * SHMEM_PAGE_SIZE);
    g_sctx_pb = sctx;   /* M3: expose to handle_query for the per-inference read */

    if (request_ring->header.magic != SHMEM_MAGIC ||
        response_ring->header.magic != SHMEM_MAGIC) {
        puts_serial("[Process B] ERROR: shared memory not initialized by Process A\n");
        goto idle;
    }
    puts_serial("[Process B] IPC rings validated\n");
    g_resp_ring = response_ring;  /* Enable pb_log IPC transport */

#if JARVIS_SMP_PROBE
    /* M2 / E1: which core did PB land on? Under SMP, a spawned process inherits
     * the creator's (node 0) affinity unless SetAffinity is called — so this is
     * expected to match PA's apic (both on core 0; AP idle) until M3 wires
     * SetAffinity. Serial-only (QEMU captures it); compare against PA's apic. */
    { puts_serial("[PB] SMP apic="); put_dec(smp_apic_id()); puts_serial("\n"); }
#endif

    /* Parse model location (GRUB module mapped by Process A) */
    uintptr_t model_vaddr = 0;
    size_t model_size = 0;
    if (argc >= 5) {
        model_vaddr = (uintptr_t)atol(argv[3]);
        model_size = (size_t)atol(argv[4]);
    }

    /* ---- Determine model source ---- */
    const void *model_data = NULL;
    size_t model_data_size = 0;

#ifdef JARVIS_HAS_MODEL
    /* Path 1: Embedded model (QEMU builds with objcopy) */
    model_data = _binary_model_gguf_start;
    model_data_size = (size_t)(_binary_model_gguf_end - _binary_model_gguf_start);
    puts_serial("[Process B] Model source: embedded .rodata (");
    put_dec((uint32_t)(model_data_size >> 20)); puts_serial("MB)\n");
#endif

    if (!model_data && model_vaddr != 0 && model_size > 0) {
        /* Path 2: GRUB multiboot module (mapped by Process A) */
        model_data = (const void *)model_vaddr;
        model_data_size = model_size;
        puts_serial("[Process B] Model source: GRUB module at ");
        put_dec((uint32_t)(model_vaddr >> 20)); puts_serial("M (");
        put_dec((uint32_t)(model_size >> 20)); puts_serial("MB)\n");
    }

    if (!model_data) {
        /* Path 3: No model — idle */
        puts_serial("[Process B] No model available — idle mode\n");
        shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
        seL4_Signal(resp_notif);
        goto idle;
    }

    /* ---- Common model loading (works for embedded and GRUB module) ---- */
    puts_serial("[Process B] Loading model: ");
    put_dec((uint32_t)(model_data_size >> 20)); puts_serial("MB\n");

    gguf_ctx_t gguf_ctx;
    int err = gguf_open_memory(&gguf_ctx, model_data, model_data_size);
    if (err) {
        puts_serial("[Process B] GGUF parse failed\n");
        goto idle;
    }

    err = qmodel_load(&qm, &gguf_ctx, model_data);
    if (err) {
        puts_serial("[Process B] Model load failed\n");
        gguf_close(&gguf_ctx);
        goto idle;
    }
    puts_serial("[Process B] Model loaded: ");
    put_dec((uint32_t)qm.config.n_layers); puts_serial(" layers, ");
    put_dec((uint32_t)qm.config.vocab_size); puts_serial(" vocab\n");

    /* Extract tokenizer */
    err = gguf_vocab_extract(model_data, model_data_size, &vocab);
    if (err) {
        puts_serial("[Process B] Vocab extraction failed\n");
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        goto idle;
    }

    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        puts_serial("[Process B] Tokenizer init failed\n");
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        goto idle;
    }
    puts_serial("[Process B] Tokenizer ready: ");
    put_dec((uint32_t)vocab.vocab_size); puts_serial(" tokens\n");

    /* Allocate inference state */
    err = llama_alloc_state(&state, &qm.config);
    if (err) {
        puts_serial("[Process B] State alloc failed (OOM?)\n");
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        goto idle;
    }

    puts_serial("[Process B] Ready for inference requests\n");
    int model_loaded = 1;

    /* ---- Probe model weight pages ----
     * Read one byte from each major tensor to verify PB can access
     * the model data mapped by PA. A page fault here means the
     * mapping is broken — better to crash during probe with a clear
     * diagnostic than silently during qmodel_forward. */
    {
        puts_serial("[Process B] Probing model weight pages...\n");
        int probe_ok = 0;

        if (qm.token_embed.data && qm.token_embed.n_bytes > 0) {
            volatile uint8_t t = *(const volatile uint8_t *)qm.token_embed.data;
            (void)t;
            puts_serial("  token_embed: OK (");
            put_dec((uint32_t)(qm.token_embed.n_bytes >> 10));
            puts_serial("KB at ");
            put_dec((uint32_t)((uintptr_t)qm.token_embed.data >> 20));
            puts_serial("M)\n");
            probe_ok++;
        }

        if (qm.output_weight.data && qm.output_weight.n_bytes > 0) {
            volatile uint8_t t1 = *(const volatile uint8_t *)qm.output_weight.data;
            volatile uint8_t t2 = *((const volatile uint8_t *)qm.output_weight.data
                                     + qm.output_weight.n_bytes - 1);
            (void)t1; (void)t2;
            puts_serial("  output_weight: OK (");
            put_dec((uint32_t)(qm.output_weight.n_bytes >> 10));
            puts_serial("KB at ");
            put_dec((uint32_t)((uintptr_t)qm.output_weight.data >> 20));
            puts_serial("M)\n");
            probe_ok++;
        }

        if (qm.config.n_layers > 0 && qm.layers) {
            if (qm.layers[0].attn_norm.data && qm.layers[0].attn_norm.n_bytes > 0) {
                volatile uint8_t t = *(const volatile uint8_t *)qm.layers[0].attn_norm.data;
                (void)t;
                puts_serial("  layer[0].attn_norm: OK\n");
                probe_ok++;
            }
            if (qm.layers[0].wq.data && qm.layers[0].wq.n_bytes > 0) {
                volatile uint8_t t1 = *(const volatile uint8_t *)qm.layers[0].wq.data;
                volatile uint8_t t2 = *((const volatile uint8_t *)qm.layers[0].wq.data
                                         + qm.layers[0].wq.n_bytes - 1);
                (void)t1; (void)t2;
                puts_serial("  layer[0].wq: OK (");
                put_dec((uint32_t)(qm.layers[0].wq.n_bytes >> 10));
                puts_serial("KB)\n");
                probe_ok++;
            }
            int last = qm.config.n_layers - 1;
            if (qm.layers[last].wq.data && qm.layers[last].wq.n_bytes > 0) {
                volatile uint8_t t1 = *(const volatile uint8_t *)qm.layers[last].wq.data;
                volatile uint8_t t2 = *((const volatile uint8_t *)qm.layers[last].wq.data
                                         + qm.layers[last].wq.n_bytes - 1);
                (void)t1; (void)t2;
                puts_serial("  layer["); put_dec((uint32_t)last);
                puts_serial("].wq: OK (");
                put_dec((uint32_t)(qm.layers[last].wq.n_bytes >> 10));
                puts_serial("KB)\n");
                probe_ok++;
            }
        }

        /* Full-sweep probe: read one byte per 4KB page across entire model */
        puts_serial("  Full sweep: ");
        put_dec((uint32_t)(model_data_size >> 20));
        puts_serial("MB (");
        put_dec((uint32_t)(model_data_size >> 12));
        puts_serial(" pages)...\n");
        {
            const volatile uint8_t *base = (const volatile uint8_t *)model_data;
            uint32_t n_pages = (uint32_t)(model_data_size >> 12);
            uint32_t checksum = 0;
            for (uint32_t p = 0; p < n_pages; p++) {
                checksum += base[p * 4096];
                if (p > 0 && (p % 50000) == 0) {
                    puts_serial("    ");
                    put_dec(p * 4 / 1024);
                    puts_serial("MB probed OK\n");
                }
            }
            puts_serial("  Full sweep OK: checksum=");
            put_dec(checksum);
            puts_serial(" ("); put_dec(n_pages);
            puts_serial(" pages)\n");
            probe_ok++;
        }

        puts_serial("[Process B] Probe complete: ");
        put_dec((uint32_t)probe_ok);
        puts_serial(" OK\n");
    }

    /* ---- Single forward pass sanity check ---- */
    {
        puts_serial("[Process B] Testing single forward pass (token 0)...\n");
        state.pos = 0;
        size_t kv_bytes = (size_t)state.kv_n_layers * state.max_seq_len *
                          qm.config.n_kv_heads * qm.config.head_dim * sizeof(float);
        memset(state.key_cache, 0, kv_bytes);
        memset(state.value_cache, 0, kv_bytes);

        qmodel_forward(&qm, &state, (int)vocab.bos_id);

        int top = sample_greedy(state.logits, qm.config.vocab_size);
        puts_serial("[Process B] Forward pass OK! top_token=");
        put_dec((uint32_t)top);
        puts_serial(" pos="); put_dec((uint32_t)state.pos);
        puts_serial("\n");
    }

    /* M3: initialize the seL4 worker pool from PA-passed argv (n_threads, done, wakes)
     * BEFORE the ready handshake, so jarvis_threads() reflects the pool on the first
     * inference. If JARVIS_SEL4_SMP is off or argc<7, jarvis_threads()==1 -> serial. */
#ifdef JARVIS_SEL4_SMP
    if (argc >= 7) {
        int n_threads = (int)atol(argv[5]);
        seL4_CPtr done = (seL4_CPtr)atol(argv[6]);
        seL4_CPtr wake[JARVIS_MAX_WORKERS]; int n_wake = 0;   /* shared cap from threadpool.h */
        for (int i = 7; i < argc && n_wake < (JARVIS_MAX_WORKERS - 1); i++)
            wake[n_wake++] = (seL4_CPtr)atol(argv[i]);
        jarvis_sel4_pool_init(n_threads, done, wake, n_wake);
#if JARVIS_RESPAWN
        /* K/M2a-2: stash the pool params so pb_restart_entry can re-init the pool on respawn.
         * Store wakes at [1..n_wake] to mirror jarvis_sel4_pool_init's own +1 indexing. */
        g_km2a_pool_n_threads = n_threads;
        g_km2a_pool_done      = done;
        g_km2a_pool_n_wake    = n_wake;
        for (int wi = 0; wi < n_wake && (wi + 1) < JARVIS_MAX_WORKERS; wi++)
            g_km2a_pool_wake[wi + 1] = wake[wi];
#endif
        puts_serial("[PB] M3 pool init: n_threads="); put_dec((uint32_t)n_threads);
        puts_serial(" workers="); put_dec((uint32_t)n_wake); puts_serial("\n");
    }
#endif

    /* Signal Process A that we're ready — eliminates startup race */
    shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
    seL4_Signal(resp_notif);

#if JARVIS_AVX2_PROBE
    /* M0: confirm the kernel enabled AVX state-saving (XCR0.AVX) at PB startup. */
    avx2_probe_init("PB");
#endif

    /* Phase 5 G2/M1 smoke: prove the 3rd shared frame (context pool) maps + reads
     * non-faulting from Process B. A real read in handle_query is M3 — this only proves
     * the page mapping. seq>=2 + the boot_id means Process A's sctx_init crossed the page. */
    {
        sctx_system_state_t scs;
        (void)sctx_read_state(sctx, &scs);
        uint32_t scq = __atomic_load_n(&sctx->seq, __ATOMIC_ACQUIRE);
        pb_log_num("[SCTX] read ok seq=", scq, "");
        pb_log_num("[SCTX] boot_id=", scs.boot_id, "");
    }

    /* ---- Main IPC loop (extracted into pb_serve_loop; §4.2 item G) ---- */
#if JARVIS_RESPAWN
    /* K/M2a-2: stash the warm context for pb_restart_entry + arm the restart stack (0xAA fill for
     * the high-water scan). The &qm/&state/&tok pointers into this frame stay valid because the
     * restart re-enters on g_km2a_restart_stack (fresh SP), preserving main()'s frame. */
    g_km2a_req_ring     = request_ring;
    g_km2a_resp_ring    = response_ring;
    g_km2a_req_notif    = req_notif;
    g_km2a_resp_notif   = resp_notif;
    g_km2a_qm           = &qm;
    g_km2a_state        = &state;
    g_km2a_tok          = &tok;
    g_km2a_bos_id       = (int)vocab.bos_id;
    g_km2a_model_loaded = model_loaded;
    memset(g_km2a_restart_stack, 0xAA, KM2A_RESTART_STACK_SIZE);
    puts_serial("[RESTART-PB] armed — baseline pointers:\n");
    km2a_puthex64("[RESTART-PB]   key_cache=", (uint64_t)(uintptr_t)state.key_cache);
    km2a_puthex64("[RESTART-PB]   layers=",    (uint64_t)(uintptr_t)qm.layers);
#endif
    pb_serve_loop(request_ring, response_ring, req_notif, resp_notif,
                  &qm, &state, &tok, (int)vocab.bos_id, model_loaded);

    /* Cleanup (unreachable in normal operation) */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    qmodel_free(&qm);
    gguf_close(&gguf_ctx);

idle:
    puts_serial("[Process B] Idle.\n");
#if JARVIS_AVX2_PROBE
    /* M0: even with no model loaded (e.g. QEMU without the slow emulated-NVMe copy),
     * exercise the AVX2-under-preemption gate in PB so the KVM run still validates YMM
     * save/restore. PB runs long YMM bursts then yields to PA (which also dirties YMM)
     * on the single core — timer preemption + Yield force cross-thread FPU switches. */
    avx2_probe_init("PB");
    while (1) {
        for (int _pi = 0; _pi < 8; _pi++)
            avx2_probe_run("PB", ((uint64_t)0xB0B0u << 8) ^ (uint64_t)_pi);
        seL4_Yield();
    }
#endif
    while (1) {
        seL4_Yield();
    }

#undef qm
#undef vocab
#undef tok
#undef state
    return 0;
}
