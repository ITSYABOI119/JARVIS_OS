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

#if JARVIS_EMBED
/* ---- Phase C / C/M1b-2: the SECOND (embedding) model, PB side --------------------------------
 * ALL file-scope, deliberately — .bss survives pb_restart_entry unconditionally, whereas a main()
 * local survives only because the restart re-enters on a separate stack and freezes main()'s
 * frame. C/M1b's §5 flagged that the FIRST model's `gguf_ctx` is still such a local, surviving by
 * that accident rather than by a stated property, and warned a second ctx would inherit the same
 * undocumented fragility. T3 RESOLVED BY CONSTRUCTION: g_pbe_ctx is file-scope, so the embed path
 * does not depend on the accident at all. (The Gemma ctx is left alone — changing it is an
 * ungated deployed-path edit for no functional gain, and is recorded as a separate carry-forward.)
 * Separate readiness flag, never g_model_bad / never the Gemma state: an embed failure degrades
 * ONLY the embed capability (C/M1b-1 T1, carried forward). */
static qmodel_t      g_pbe_qm;
static gguf_vocab_t  g_pbe_vocab;
static tokenizer_t   g_pbe_tok;
static llama_state_t g_pbe_state;
static gguf_ctx_t    g_pbe_ctx;
static int           g_pbe_ready = 0;

/* Embed context cap. 64 tokens, per the C/M1b OQ1 ruling: C/M0.5 measured that SYMMETRIC
 * query-to-query beats asymmetric, so C/M2 embeds the stored QUERY, and a control-IN query is
 * hard-capped at 172 BYTES ~= 40-55 tokens. 64 covers that with headroom at ~14.7 MiB of state.
 * COUPLING TO RECORD: if C/M2 ever embeds ANSWERS (<=512 B ~= 130 tokens) this is a HEAP
 * RE-BUDGET, not a free switch — at 128 tokens the KV alone is 28 MiB, at the 512 default 112 MiB
 * (which does not fit beside Gemma in the 128 MiB musl heap at all). */
#define EMBED_CTX_TOKENS 64

/* T1: exact heap accounting. sys_morecore.c backs musl's brk with a static array — morecore_area
 * is a global symbol and sbrk(0) returns the current break — so (break - area) is the EXACT bytes
 * consumed, not an estimate. CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES is the ceiling (128 MiB). */
#include <unistd.h>                 /* sbrk */
extern char      morecore_area[];   /* sys_morecore.c: the static heap region (global symbol)   */
extern uintptr_t morecore_top;      /* sys_morecore.c: its end — read this rather than the      */
                                    /* CONFIG_ macro, so the report cannot disagree with the    */
                                    /* allocator that is actually running.                      */
static unsigned long pbe_heap_used(void)
{
    return (unsigned long)((uintptr_t)sbrk(0) - (uintptr_t)morecore_area);
}
/* pbe_heap_report is defined AFTER puts_serial/put_dec (they are statics declared later in this
 * file), see below. */
static void pbe_heap_report(const char *when);
#endif /* JARVIS_EMBED */

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

#if JARVIS_EMBED
/* C/M1b-2 T1 (definition; forward-declared above because puts_serial/put_dec are statics defined
 * only just now). EXACT heap accounting from the allocator's own symbols. */
static void pbe_heap_report(const char *when)
{
    /* Report the BREAK POSITION only. An earlier version also printed a total/free derived from
     * morecore_top — and the box falsified it: the "total" CHANGED between two calls in one boot
     * (80668 KB -> 56276 KB), so morecore_top is mutated at runtime and any free/percent derived
     * from it is fiction. Two further honesty limits on even the `used` figure, stated here rather
     * than implied: it is the musl BREAK, so (a) it is a LOWER BOUND on bytes allocated, because
     * malloc satisfies requests from already-broken-but-free space without moving the break, and
     * (b) the delta between two marks is likewise a lower bound on what happened between them. It
     * is exact for what it claims — where the break is — and nothing more. */
    puts_serial("[EMBED-HEAP] "); puts_serial(when);
    puts_serial(": brk_used="); put_dec((uint32_t)(pbe_heap_used() >> 10));
    puts_serial("KB (lower bound; malloc reuses free space without moving brk)\n");
}
#endif

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

/* Bounded retry for a full response ring. Each retry costs one signal + one yield, so this is
 * microseconds, not a stall: it is a back-pressure window for PA to drain, NOT a wait for a dead
 * PA. If PA really is gone the existing K/M2c miss-counter and its respawn are the right recovery,
 * so giving up quickly and loudly is deliberate. */
#ifndef PB_SEND_RETRY_MAX
#define PB_SEND_RETRY_MAX 256
#endif

/* Response-chunk send, wrapped ONLY so the ring-full branch can be induced. That branch had never
 * executed in a deployed build (text_out[512] capped a response at <=3 chunks into a 15-slot ring),
 * and commit 2 of this milestone raises exactly those ceilings — so it must be proven to work
 * BEFORE it becomes reachable, not after.
 *   JARVIS_RING_PROBE 1 = fail the first PB_RING_PROBE_FAILS sends this boot, then behave normally
 *                         => exercises the RETRY path; the answer must still arrive INTACT
 *   JARVIS_RING_PROBE 2 = fail EVERY send => exercises EXHAUSTION: the loud truncation log, and a
 *                         short-but-contiguous answer rather than a holed one */
static int pb_send_chunk(shmem_ring_t *ring, uint16_t seq, const char *buf, int len)
{
#if JARVIS_RING_PROBE == 2
    (void)ring; (void)seq; (void)buf; (void)len;
    return -1;
#else
#if JARVIS_RING_PROBE == 1
    static int probe_fails = 0;
    if (probe_fails < PB_RING_PROBE_FAILS) { probe_fails++; return -1; }
#endif
    return shmem_ipc_send(ring, MSG_RESPONSE, seq, buf, (uint16_t)len);
#endif
}

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
     * (all ten VERIFIED against the GGUF's tokenizer.ggml.tokens, 2026-07-26.)
     *
     * CORRECTED 2026-07-26: this used to read "Stop on <eos>=1, NOT eos_id=106 (that's <turn|>
     * which model emits first)". That had it backwards — <turn|> IS the model declaring its turn
     * finished, and ignoring it made every answer pad to the token cap. The generation loop now
     * stops on tok->eos_id (the LOADED declared value) as well as <eos>. */
    int prompt_ids[256];          /* G3/M2: was [128]; room for preamble+query. KV stays 512. */
    int n_prompt = 0;
    prompt_ids[n_prompt++] = bos_id;        /* <bos> */

#if JARVIS_THINKING
    /* THINKING MODE, placed where the MODEL'S OWN chat_template puts it: a LEADING system turn,
     *     '<|turn>system\n'  '<|think|>\n'  '<turn|>\n'
     * gated in the template on `enable_thinking`. The role word is ordinary TEXT (only <|turn>,
     * <|think|> and <turn|> are tokens), so it is encoded rather than hardcoded as an id —
     * hardcoding ids is the habit that produced this bug class.
     *
     * The old code appended <|think|> as the LAST prompt token, after '<|turn>model\n' — i.e. in
     * the slot where the ANSWER begins, a position the template never emits. See jarvis_debug.h
     * for the measured placement comparison and why this flag defaults OFF. */
    prompt_ids[n_prompt++] = 105;           /* <|turn> */
    n_prompt += tokenizer_encode(tok, "system", prompt_ids + n_prompt, 8);
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 98;            /* <|think|> */
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 106;           /* <turn|> */
    prompt_ids[n_prompt++] = 107;           /* \n */
#endif

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
    /* NO <|think|> here. The template's generation prompt is a bare '<|turn>model\n'; the think
     * token belongs in the leading system turn above (gated JARVIS_THINKING), not in the slot
     * where the answer starts. Putting it here is what made the box emit <|channel>thought
     * instead of answering. */

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

        /* HONOUR THE MODEL'S DECLARED END-OF-TURN.
         *
         * The model declares its terminator in the GGUF as tokenizer.ggml.eos_token_id, which
         * gguf_vocab.c ALREADY reads and tokenizer_init already copies into tok->eos_id — for
         * Gemma 4 that is 106 (<turn|>). This loop used to break only on <eos>=1 and ignore the
         * declared value, so the model would end its turn, not be listened to, and keep
         * generating until the cap. MEASURED on a native probe: an answer completed, then 23
         * consecutive <turn|> tokens burned before the 50-token cap — 46% of the budget spent
         * padding a finished answer.
         *
         * Use the LOADED value, never a hardcoded 106: hardcoding token ids is exactly what
         * produced this bug class (the <|think|> placement bug came from the same habit).
         * eos_id > 0 guards a model that declares none; <eos>=1 is kept as a belt-and-braces
         * stop for a model whose declared id is absent or wrong.
         *
         * BREAK BEFORE STORING. A terminator is not answer text. Storing it is why records and
         * console replies carried a trailing "<turn|><turn|><eos>", and why that junk reached
         * the recall corpus. n_gen therefore counts ANSWER tokens only, which also makes the
         * v4 tok/s figure measure generation rather than padding. */
        if (next == 1 /* <eos> */ || (tok->eos_id > 0 && next == tok->eos_id))
            break;

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
        /* (the terminator check moved to the TOP of the loop — see the comment there) */
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
    /* A DROPPED CHUNK MUST NEVER PUNCH A SILENT HOLE IN THE ANSWER.
     *
     * shmem_ipc_send returns -1 when the ring is FULL (shmem_ipc.c: `wr - rd >= size`). This loop
     * used to do `(void)rc` and then `offset += chunk` regardless, so a full ring silently DROPPED
     * that chunk and the answer was reassembled by PA with a hole in the middle — no error, no log,
     * just missing text. It was latent only because text_out[512] capped a response at <=3 chunks
     * into a 15-slot ring. Raising the ceilings without fixing this first would make every new
     * overflow fail QUIETLY, which is the worst way to discover a limit has been overshot.
     *
     * CHOSEN FIX: RETRY THE SAME OFFSET WITH BACK-PRESSURE, rather than fail-and-mark. A full ring
     * here is TRANSIENT — PA is polling this ring and drains it continuously, and PA shares this
     * core (K/M2c), so signalling + yielding is precisely what lets it run and make room. Retrying
     * PRESERVES the answer, which is a strictly better outcome than reporting a degraded one; and
     * marking the reply degraded on the wire would need a new message type or field, i.e. a wire
     * change, for a case the retry makes nearly unreachable.
     *
     * The offset is advanced ONLY after a send that actually succeeded, so the answer can be short
     * but is always CONTIGUOUS — never holed.
     *
     * If the bounded retry is exhausted (PA wedged or dead), STOP and say so LOUDLY. Note the log
     * MUST use puts_serial, not pb_log: pb_log sends MSG_DEBUG over this very ring, which is the
     * thing that is full. A short answer plus a loud line beats a silent hole. */
    while (offset < text_len) {
        int chunk = text_len - offset;
        if (chunk > SHMEM_MAX_PAYLOAD) chunk = SHMEM_MAX_PAYLOAD;
#if JARVIS_DBG_RING
        puts_serial("[PB] chunk @"); put_dec((uint32_t)offset);
        puts_serial(" len="); put_dec((uint32_t)chunk); puts_serial("\n");
#endif
        int rc = pb_send_chunk(response_ring, msg_seq, text_out + offset, chunk);
#if JARVIS_DBG_RING
        puts_serial("[PB] send rc="); put_dec((uint32_t)rc); puts_serial("\n");
#endif
        if (rc < 0) {
            /* Retry the SAME offset, yielding so PA can drain. */
            int tries = 0;
            while (rc < 0 && tries < PB_SEND_RETRY_MAX) {
                seL4_Signal(resp_notif);   /* nudge PA in case it is waiting rather than polling */
                seL4_Yield();              /* PA shares this core — yield is what lets it drain */
                rc = pb_send_chunk(response_ring, msg_seq, text_out + offset, chunk);
                tries++;
            }
            if (rc < 0) {
                puts_serial("[PB] RESPONSE TRUNCATED: response ring full after ");
                put_dec((uint32_t)PB_SEND_RETRY_MAX);
                puts_serial(" retries, undelivered bytes=");
                put_dec((uint32_t)(text_len - offset));
                puts_serial(" (answer is short but CONTIGUOUS — no hole)\n");
                break;                     /* offset NOT advanced — nothing was sent */
            }
#if JARVIS_DBG_RING
            puts_serial("[PB] send recovered after retries=");
            put_dec((uint32_t)tries); puts_serial("\n");
#endif
        }
        offset += chunk;                   /* ONLY after a send that succeeded */
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
#if JARVIS_EMBED
    /* C/M1b-2 T2: the four pointers above are ALL GEMMA'S. With a second model resident they stay
     * byte-identical even if the EMBED state leaks or re-allocs entirely — the zero-RESOURCE gate
     * does not weaken gradually, it stops covering the thing this milestone introduced. Extend the
     * baseline with the embed state's own heap pointers plus the live heap high-water, so a leak
     * on either side is visible. (A respawn does NOT re-run main(), so these must be IDENTICAL
     * across every cycle; a change means something re-allocated behind the restart.) */
    if (g_pbe_ready) {
        km2a_puthex64("[RESTART-PB]   e_key_cache=",   (uint64_t)(uintptr_t)g_pbe_state.key_cache);
        km2a_puthex64("[RESTART-PB]   e_value_cache=", (uint64_t)(uintptr_t)g_pbe_state.value_cache);
        km2a_puthex64("[RESTART-PB]   e_logits=",      (uint64_t)(uintptr_t)g_pbe_state.logits);
        km2a_puthex64("[RESTART-PB]   e_layers=",      (uint64_t)(uintptr_t)g_pbe_qm.layers);
        puts_serial("[RESTART-PB]   heap_brk_KB="); km2a_putdec((uint32_t)(pbe_heap_used() >> 10));
        puts_serial("\n");
#if JARVIS_EMBED_PROBE
        /* "A post-respawn embed still correct" — the pointers being flat proves nothing was
         * re-allocated; this proves the model is still USABLE through the reused state. One fixed
         * probe, and its first three components are printed so the value can be compared against
         * the boot-time run for THAT probe (they must be bit-identical: same weights, same state,
         * same code — nothing about a respawn should perturb the arithmetic). */
        {
            static const char *EP0 = "what is a page fault";
            int ids[64];
            int n = tokenizer_encode(&g_pbe_tok, EP0, ids, 62);
            if (n > 0) {
                ids[n++] = 151643;
                size_t ekv = (size_t)g_pbe_state.kv_n_layers * (size_t)g_pbe_state.max_seq_len *
                             (size_t)g_pbe_qm.config.n_kv_heads * (size_t)g_pbe_qm.config.head_dim *
                             sizeof(float);
                g_pbe_state.pos = 0;
                memset(g_pbe_state.key_cache,   0, ekv);
                memset(g_pbe_state.value_cache, 0, ekv);
                static float rvec[1024];
                qmodel_embed_last(&g_pbe_qm, &g_pbe_state, ids, n, rvec);
                uint32_t w0, w1, w2;
                memcpy(&w0, &rvec[0], 4); memcpy(&w1, &rvec[1], 4); memcpy(&w2, &rvec[2], 4);
                km2a_puthex64("[RESTART-PB]   e_v0=", (uint64_t)w0);
                km2a_puthex64("[RESTART-PB]   e_v1=", (uint64_t)w1);
                km2a_puthex64("[RESTART-PB]   e_v2=", (uint64_t)w2);
            }
        }
#endif
    }
#endif
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

#if JARVIS_EMBED
    /* ---- C/M1b-2: the embedding model, PB side. PROBE-driven, NO IPC (that is C/M1b-3). ----
     * T1: measure GEMMA'S heap FIRST. The C/M1b design recorded Gemma's live llama_state_t
     * consumption as NOT VERIFIED, which made "how much of the 128 MiB is actually free" the first
     * unknown this milestone had to close — and it must be closed BEFORE allocating, because if
     * Gemma already consumes more than expected the 64-token cap needs re-deriving from a real
     * number rather than from arithmetic. */
    pbe_heap_report("after-gemma-state");
    {
        uintptr_t evaddr = 0; size_t esize = 0;
        /* PA appends the embed base/size AFTER the per-worker wake cptrs; the wake block is
         * exactly (n_threads-1) long, so their index is deterministic. n_threads is read straight
         * from argv[5] rather than from g_km2a_pool_n_threads, because the pool is initialised
         * LATER in main() (~:1092) than this block runs — using the stash here would silently
         * compute index 7 and read a wake cptr as the embed base. */
        int e_nthreads = (argc >= 7) ? (int)atol(argv[5]) : 1;
        int ebase_idx  = 7 + (e_nthreads > 0 ? e_nthreads - 1 : 0);
        if (argc >= ebase_idx + 2) {
            evaddr = (uintptr_t)atol(argv[ebase_idx]);
            esize  = (size_t)atol(argv[ebase_idx + 1]);
        }
        if (!evaddr || !esize) {
            puts_serial("[EMBED-PB] no embed model in argv - embed capability OFF "
                        "(normal; Gemma unaffected)\n");
        } else {
            puts_serial("[EMBED-PB] embed model at vaddr "); put_dec((uint32_t)(evaddr >> 20));
            puts_serial("M size "); put_dec((uint32_t)(esize >> 20)); puts_serial("MB\n");
            if (gguf_open_memory(&g_pbe_ctx, (const void *)evaddr, esize) != 0) {
                puts_serial("[EMBED-PB] GGUF parse failed - embed OFF\n");
            } else if (qmodel_load(&g_pbe_qm, &g_pbe_ctx, (const void *)evaddr) != 0) {
                puts_serial("[EMBED-PB] qmodel_load failed - embed OFF\n");
            } else {
                /* T4 + the 64-token cap. The cap is applied to config->max_seq_len AFTER load and
                 * BEFORE llama_alloc_state — grep-proven safe: LLAMA_MAX_SEQ_LEN appears only as a
                 * default/clamp at llama_load.c:146-148, and every allocation sizes off
                 * config->max_seq_len (llama_load.c:691). Without this the embed state would want
                 * ~113 MiB of a 128 MiB heap Gemma is already using — a cycle-1 brick. */
                int was = g_pbe_qm.config.max_seq_len;
                if (g_pbe_qm.config.max_seq_len > EMBED_CTX_TOKENS)
                    g_pbe_qm.config.max_seq_len = EMBED_CTX_TOKENS;
                puts_serial("[EMBED-PB] ctx cap "); put_dec((uint32_t)was);
                puts_serial(" -> "); put_dec((uint32_t)g_pbe_qm.config.max_seq_len);
                puts_serial(" tokens\n");
                if (gguf_vocab_extract((const void *)evaddr, esize, &g_pbe_vocab) != 0) {
                    puts_serial("[EMBED-PB] vocab extract failed - embed OFF\n");
                    qmodel_free(&g_pbe_qm);
                } else if (gguf_vocab_init_tokenizer(&g_pbe_vocab, &g_pbe_tok) != 0) {
                    puts_serial("[EMBED-PB] tokenizer init failed - embed OFF\n");
                    gguf_vocab_free(&g_pbe_vocab); qmodel_free(&g_pbe_qm);
                } else if (llama_alloc_state(&g_pbe_state, &g_pbe_qm.config) != 0) {
                    /* T4: llama_alloc_state validates every calloc and FREES ITS OWN partials via
                     * llama_free_state before returning -1 (llama_load.c:818-827), so a failed
                     * embed alloc does NOT permanently shrink the heap — the exposure the trap
                     * asked about is bounded by musl's free, not by the seL4 allocator (which is
                     * the never-frees one; this is the musl heap and free() works normally here).
                     * Gemma is untouched either way. */
                    puts_serial("[EMBED-PB] state alloc FAILED (OOM) - embed OFF; Gemma unaffected\n");
                    tokenizer_free(&g_pbe_tok); gguf_vocab_free(&g_pbe_vocab);
                    qmodel_free(&g_pbe_qm);
                    pbe_heap_report("after-failed-embed-state");
                } else {
                    g_pbe_ready = 1;
                    puts_serial("[EMBED-PB] ready: "); put_dec((uint32_t)g_pbe_qm.config.n_layers);
                    puts_serial(" layers dim "); put_dec((uint32_t)g_pbe_qm.config.dim);
                    puts_serial(" kv_layers "); put_dec((uint32_t)g_pbe_state.kv_n_layers);
                    puts_serial("\n");
                    pbe_heap_report("after-embed-state");
                }
            }
        }
    }
#endif /* JARVIS_EMBED */

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
#if JARVIS_EMBED
        /* C/M1b-2: PA appends the embed base/size after the wake block, so this loop must stop at
         * the end of the wake block instead of walking to argc — otherwise it would consume the
         * embed args as wake cptrs. Bounded by n_threads, which is exactly how many PA wrote.
         * Gated so the EMBED=0 object is unchanged (the two forms are equivalent today, since PA
         * passes exactly workers_started caps and argc == 7 + workers_started). */
        int wake_end = 7 + (n_threads > 0 ? n_threads - 1 : 0);
        if (wake_end > argc) wake_end = argc;
#else
        int wake_end = argc;
#endif
        for (int i = 7; i < wake_end && n_wake < (JARVIS_MAX_WORKERS - 1); i++)
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
#if JARVIS_EMBED_PROBE
    /* ---- C/M1b-2 parity + latency probe. Runs AFTER the pool init on purpose: the embed forward
     * goes through the SAME threaded qmatmul path a real embed would (JARVIS_SEL4_SMP =>
     * JARVIS_PARALLEL), so this measures the real thing, and the ~1e-6 divergence from the
     * single-threaded host harness is expected and explained rather than discovered mid-gate. ---- */
    if (g_pbe_ready) {
        static const char *const EPROBES[] = {
            "what is a page fault", "how does dns work", "what is a mutex",
            "how do you find a value in a sorted array in logarithmic time",
            "what lightweight text format stores structured data as key-value pairs",
            "how do you protect a critical section so only one thread runs it",
            "what's the capital of France", "how do you bake sourdough bread",
            "how long have you been up", "what model are you running",
            "why doesn't adding more cpu cores speed up a single-threaded program",
            "what is a hash table", "explain how tcp handshake works",
            "what is public-key encryption", "what does DMA do",
        };
        const int n_ep = (int)(sizeof(EPROBES) / sizeof(EPROBES[0]));
        const int edim = g_pbe_qm.config.dim;
        static const char HX[] = "0123456789abcdef";
        float *evec = (float *)malloc((size_t)edim * sizeof(float));
        size_t ekv = (size_t)g_pbe_state.kv_n_layers * (size_t)g_pbe_state.max_seq_len *
                     (size_t)g_pbe_qm.config.n_kv_heads * (size_t)g_pbe_qm.config.head_dim * sizeof(float);
        puts_serial("[EMBED-PROBE] begin n="); put_dec((uint32_t)n_ep);
        puts_serial(" dim="); put_dec((uint32_t)edim);
        puts_serial(" threads="); put_dec((uint32_t)jarvis_threads()); puts_serial("\n");
        for (int p = 0; evec && p < n_ep; p++) {
            int ids[128];
            int n = tokenizer_encode(&g_pbe_tok, EPROBES[p], ids, 127);
            if (n < 0) { puts_serial("[EMBED-PROBE] encode failed\n"); break; }
            ids[n++] = 151643;                       /* EOS, appended + last-pooled (C/M1a) */
            g_pbe_state.pos = 0;                      /* per-sequence reset — the caller's job */
            memset(g_pbe_state.key_cache,   0, ekv);
            memset(g_pbe_state.value_cache, 0, ekv);
            uint64_t t0 = m1_rdtsc();
            qmodel_embed_last(&g_pbe_qm, &g_pbe_state, ids, n, evec);
            uint64_t cyc = m1_rdtsc() - t0;
            puts_serial("[EMBED-PROBE] p="); put_dec((uint32_t)p);
            puts_serial(" toks="); put_dec((uint32_t)n);
            puts_serial(" cyc="); put_dec((uint32_t)(cyc / 1000)); puts_serial("k\n");
            /* raw little-endian float32 as hex, so the host can byte-compare AND measure the
             * divergence magnitude. One line per probe. */
            puts_serial("[EV] "); put_dec((uint32_t)p); seL4_DebugPutChar(' ');
            for (int i = 0; i < edim; i++) {
                uint32_t w; memcpy(&w, &evec[i], 4);
                for (int s = 28; s >= 0; s -= 4) seL4_DebugPutChar(HX[(w >> s) & 0xF]);
            }
            seL4_DebugPutChar('\n');
        }
        free(evec);
        pbe_heap_report("after-embed-probe");
        puts_serial("[EMBED-PROBE] end\n");
    }
#endif /* JARVIS_EMBED_PROBE */

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
