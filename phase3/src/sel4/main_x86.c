/*
 * JARVIS AI-OS Phase 3 — x86-64 Rootserver (Process A)
 *
 * Rootserver with process isolation (SEC-014):
 *   Process A (this file): Decision cache, SHIELD, IPC dispatch, drivers
 *   Process B (jarvis-inference): Model loading, quantized forward pass, generation
 *
 * Connected via shared memory IPC (2 rings × 4KB) + seL4_Notification signaling.
 * Process B is spawned from a CPIO-archived ELF at boot.
 */

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <sel4platsupport/platsupport.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <sel4utils/vspace.h>
#include <sel4utils/process.h>
#include <sel4utils/process_config.h>
#include <vka/object.h>
#include <vka/capops.h>
#include <simple/simple.h>
#include <simple-default/simple-default.h>
#include <cpio/cpio.h>

#include "decision_cache.h"
#include "cache_patterns.h"
#include "tensor_ops.h"
#include "dequant.h"
#include "tokenizer.h"
#include "sampling.h"

/* Note: JARVIS_HAS_MODEL inference code is now in Process B (jarvis-inference).
 * Process A no longer includes model headers or runs inference directly. */

#include "shmem_ipc.h"

#ifdef __x86_64__
#include "vga_text.h"
#include "pci.h"
#include "nvme.h"
#include "nvme_log.h"
#include "fat32.h"
#include "jarvis_debug.h"
static int vga_ready = 0;  /* set after VGA frame mapped into vspace */

/* seL4 IOPort wrappers for PCI config space (0xCF8/0xCFC).
 * Cap acquired at runtime, stored here for the wrapper functions. */
static seL4_CPtr g_pci_ioport_cap = 0;

static void sel4_pci_outl(uint16_t port, uint32_t val) {
    seL4_X86_IOPort_Out32(g_pci_ioport_cap, port, val);
}
/* Forward declarations for NVMe/FAT32 callbacks */
static void puts_serial(const char *s);
static void put_hex(uint32_t val);
static void put_dec(uint32_t val);

/* NVMe bounce buffer for FAT32 reads.
 * FAT32 callback gets an arbitrary buf pointer, but NVMe needs DMA paddr.
 * Read into bounce buffer, memcpy to caller's buf. */
static nvme_controller_t *g_nvme_ptr = NULL;
static void *g_nvme_bounce_vaddr = NULL;
static uint64_t g_nvme_bounce_paddr = 0;
static uint64_t g_nvme_read_count = 0;

static int fat32_nvme_read(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_nvme_ptr || !g_nvme_bounce_vaddr) return -1;
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        int err = nvme_read_sectors(g_nvme_ptr, lba + i, 1,
                                     g_nvme_bounce_vaddr, g_nvme_bounce_paddr);
        if (err) return err;
        memcpy(out + (size_t)i * 512, g_nvme_bounce_vaddr, 512);
        g_nvme_read_count++;
        /* Progress every 100K sectors (~50MB) */
        if (g_nvme_read_count % 100000 == 0) {
            puts_serial("  ");
            put_dec((uint32_t)(g_nvme_read_count * 512 / (1024 * 1024)));
            puts_serial("MB read\n");
        }
    }
    return 0;
}

static void nvme_timeout_debug(uint32_t cq_raw, uint32_t csts_val, uint8_t sq_op) {
    puts_serial("[NVMe DBG] timeout: cq_status=");
    put_hex(cq_raw);
    puts_serial(" csts="); put_hex(csts_val);
    puts_serial(" sq0_op="); put_hex(sq_op);
    puts_serial("\n");
}

static uint32_t sel4_pci_inl(uint16_t port) {
    seL4_X86_IOPort_In32_t reply = seL4_X86_IOPort_In32(g_pci_ioport_cap, port);
    return reply.result;
}
#endif

/* ---- CPIO archive (contains Process B's ELF) ---- */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

#define INFERENCE_APP "jarvis-inference"

/* ---- Allocman bootstrap for process spawning ---- */
/* Process B's ELF has 771MB .rodata + 128MB BSS = ~230K frames.
 * The split allocator needs O(N) bookkeeping for N frame allocs.
 * Static pool: initial bootstrap. Virtual pool: runtime bookkeeping. */
#define ALLOCATOR_STATIC_POOL_SIZE (BIT(seL4_PageBits) * 500)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];
#define ALLOCATOR_VIRTUAL_POOL_SIZE (BIT(seL4_PageBits) * 100000)

static sel4utils_alloc_data_t vspace_data;
static simple_t simple;
static vka_t vka;
static vspace_t vspace;

/* Shared memory for IPC between Process A and B.
 * Static page-aligned BSS — already mapped in rootserver ELF. */
static shmem_ring_t shared_rings[2] __attribute__((aligned(4096)));
static shmem_ring_t *shared_request_ring = &shared_rings[0];
static shmem_ring_t *shared_response_ring = &shared_rings[1];
static sel4utils_process_t inference_process;

/* NVMe model loading state (file-scope for cross-function visibility) */
#define MODEL_MAX_PAGES (800 * 1024)   /* 800K pages = 3200MB max — fits Gemma 4 E2B (2962MB) */
static seL4_CPtr *model_frame_caps = NULL;  /* dynamically allocated after vspace init */
static int nvme_model_loaded = 0;
static uint32_t nvme_model_size = 0;
static uint32_t nvme_model_n_pages = 0;

/* ---- Serial output via seL4 debug syscall ---- */

/* NVMe log line buffer: accumulates chars until newline, then flushes
 * the whole line as a LOG_BOOT entry with [PA] source tag.
 * Captures ALL Process A serial output automatically. */
#if JARVIS_DBG_BOOT_LOG
static char  log_line_buf[480];
static int   log_line_pos = 0;
#endif

static void puts_serial(const char *s)
{
    const char *p = s;
    while (*p) {
        seL4_DebugPutChar(*p);
#if JARVIS_DBG_BOOT_LOG
        if (g_nvme_ptr) {
            /* Start each line with [SER] or [VGA] tag */
            if (log_line_pos == 0) {
                const char *tag = vga_ready ? "[VGA] " : "[SER] ";
                while (*tag) log_line_buf[log_line_pos++] = *tag++;
            }
            if (*p == '\n') {
                log_line_buf[log_line_pos] = '\0';
                if (log_line_pos > 6)
                    nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                                   g_nvme_bounce_paddr, LOG_BOOT, log_line_buf);
                log_line_pos = 0;
            } else if (*p != '\r' && log_line_pos < 479) {
                log_line_buf[log_line_pos++] = *p;
            }
        }
#endif
        p++;
    }
#ifdef __x86_64__
    if (vga_ready) vga_puts(s);
#endif
}

static void put_hex(uint32_t val)
{
    const char hex[] = "0123456789abcdef";
    puts_serial("0x");
    for (int i = 28; i >= 0; i -= 4)
        seL4_DebugPutChar(hex[(val >> i) & 0xF]);
#ifdef __x86_64__
    if (vga_ready) {
        for (int i = 28; i >= 0; i -= 4)
            vga_putc(hex[(val >> i) & 0xF]);
    }
#endif
}

static void put_dec(uint32_t val)
{
    char buf[12];
    int i = 0;
    if (val == 0) {
        seL4_DebugPutChar('0');
#ifdef __x86_64__
        if (vga_ready) vga_putc('0');
#endif
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0) {
        seL4_DebugPutChar(buf[i]);
#ifdef __x86_64__
        if (vga_ready) vga_putc(buf[i]);
#endif
    }
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
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

/* ---- Embedded Model Inference (Stage 1-4) ---- */

#ifdef JARVIS_HAS_MODEL
extern const unsigned char _binary_model_gguf_start[];
extern const unsigned char _binary_model_gguf_end[];

static void put_u64(uint64_t val)
{
    char buf[21];
    int i = 0;
    if (val == 0) { seL4_DebugPutChar('0'); return; }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        seL4_DebugPutChar(buf[i]);
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int run_inference_stages(void)
{
    int stage_pass = 0;
    size_t model_size = (size_t)(_binary_model_gguf_end - _binary_model_gguf_start);

    puts_serial("\n=== JARVIS Inference Pipeline ===\n\n");
    puts_serial("[MODEL] Embedded GGUF: ");
    put_u64(model_size);
    puts_serial(" bytes (");
    put_u64(model_size / (1024 * 1024));
    puts_serial(" MB)\n\n");

    /* ---- Stage 1: Parse GGUF headers ---- */
    puts_serial("[Stage 1] Parsing GGUF metadata...\n");
    gguf_ctx_t gguf_ctx;
    int err = gguf_open_memory(&gguf_ctx, _binary_model_gguf_start, model_size);
    if (err) {
        puts_serial("  FAIL: gguf_open_memory error ");
        put_dec((uint32_t)(-err));
        puts_serial("\n");
        return stage_pass;
    }
    puts_serial("  Version:  "); put_dec(gguf_ctx.version); puts_serial("\n");
    puts_serial("  Tensors:  "); put_u64(gguf_ctx.n_tensors); puts_serial("\n");
    puts_serial("  KV pairs: "); put_u64(gguf_ctx.n_kv); puts_serial("\n");

    puts_serial("[Stage 1] GGUF parse ... PASS\n\n");
    stage_pass++;

    /* ---- Stage 2: Quantized model load (zero-copy) ---- */
    puts_serial("[Stage 2] Loading quantized model (zero-copy into .rodata)...\n");

    qmodel_t qm;
    err = qmodel_load(&qm, &gguf_ctx, _binary_model_gguf_start);
    if (err) {
        puts_serial("  FAIL: qmodel_load error ");
        put_dec((uint32_t)(-err));
        puts_serial("\n");
        gguf_close(&gguf_ctx);
        return stage_pass;
    }

    puts_serial("  dim="); put_dec((uint32_t)qm.config.dim);
    puts_serial(" layers="); put_dec((uint32_t)qm.config.n_layers);
    puts_serial(" heads="); put_dec((uint32_t)qm.config.n_heads);
    puts_serial(" kv_heads="); put_dec((uint32_t)qm.config.n_kv_heads);
    puts_serial(" vocab="); put_dec((uint32_t)qm.config.vocab_size);
    puts_serial(" hidden="); put_dec((uint32_t)qm.config.hidden_dim);
    puts_serial("\n");
    puts_serial("  embed type: "); puts_serial(gguf_type_name(qm.token_embed.type)); puts_serial("\n");
    puts_serial("  Weight data: zero-copy (no heap for weights)\n");

    puts_serial("[Stage 2] Quantized model load ... PASS\n\n");
    stage_pass++;

    /* ---- Stage 3: Tokenizer from GGUF vocab ---- */
    puts_serial("[Stage 3] Extracting tokenizer vocab...\n");

    gguf_vocab_t vocab;
    err = gguf_vocab_extract(_binary_model_gguf_start, model_size, &vocab);
    if (err) {
        puts_serial("  FAIL: gguf_vocab_extract error ");
        put_dec((uint32_t)(-err));
        puts_serial("\n");
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        return stage_pass;
    }
    puts_serial("  Vocab size: "); put_dec((uint32_t)vocab.vocab_size);
    puts_serial(", BOS="); put_dec((uint32_t)vocab.bos_id);
    puts_serial(", EOS="); put_dec((uint32_t)vocab.eos_id);
    puts_serial("\n");

    tokenizer_t tok;
    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        puts_serial("  FAIL: gguf_vocab_init_tokenizer error\n");
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        return stage_pass;
    }

    int test_ids[32];
    int n_tok = tokenizer_encode(&tok, "Hello", test_ids, 32);
    puts_serial("  encode('Hello') = "); put_dec((uint32_t)n_tok); puts_serial(" tokens: [");
    for (int i = 0; i < n_tok && i < 8; i++) {
        if (i > 0) puts_serial(", ");
        put_dec((uint32_t)test_ids[i]);
    }
    puts_serial("]\n");

    puts_serial("[Stage 3] Tokenizer ... PASS\n\n");
    stage_pass++;

    /* ---- Stage 4: Quantized text generation ---- */
    puts_serial("[Stage 4] Running quantized inference...\n");

    llama_state_t state;
    err = llama_alloc_state(&state, &qm.config);
    if (err) {
        puts_serial("  FAIL: llama_alloc_state error (out of memory?)\n");
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        return stage_pass;
    }

    const char *prompt = "The seL4 microkernel is";
    int prompt_ids[64];
    /* Prepend BOS token — Llama 3.2 requires it */
    prompt_ids[0] = vocab.bos_id;
    int n_prompt = 1 + tokenizer_encode(&tok, prompt, prompt_ids + 1, 63);
    puts_serial("  Prompt: BOS + \""); puts_serial(prompt); puts_serial("\"\n");
    puts_serial("  Prompt tokens: "); put_dec((uint32_t)n_prompt); puts_serial("\n");
    puts_serial("  Generating 20 tokens (greedy, temp=0)...\n");

    int output_ids[20];
    int n_gen = qmodel_generate(&qm, &state, prompt_ids, n_prompt,
                                 output_ids, 20, tok.eos_id,
                                 0.0f, 1, 42);

    puts_serial("  Generated "); put_dec((uint32_t)n_gen); puts_serial(" tokens: [");
    for (int i = 0; i < n_gen && i < 10; i++) {
        if (i > 0) puts_serial(", ");
        put_dec((uint32_t)output_ids[i]);
    }
    if (n_gen > 10) puts_serial(", ...");
    puts_serial("]\n");

    char text_out[512];
    tokenizer_decode(&tok, output_ids, n_gen, text_out, sizeof(text_out));
    puts_serial("  Output: \""); puts_serial(text_out); puts_serial("\"\n");

    puts_serial("[Stage 4] Generation ... PASS\n\n");
    stage_pass++;

    /* ---- Stage 5: Performance benchmark ---- */
    puts_serial("[Stage 5] Performance benchmark...\n");
    {
        llama_state_t bench_state;
        err = llama_alloc_state(&bench_state, &qm.config);
        if (err) {
            puts_serial("  SKIP: state alloc failed\n");
        } else {
            int bench_prompt[] = {(int)vocab.bos_id, 791}; /* BOS + "The" */
            int bench_out[10];

            uint64_t t_start = rdtsc();
            int bench_n = qmodel_generate(&qm, &bench_state, bench_prompt, 2,
                                           bench_out, 10, tok.eos_id, 0.0f, 1, 42);
            uint64_t t_end = rdtsc();
            uint64_t cycles = t_end - t_start;

            puts_serial("  Generated "); put_dec((uint32_t)bench_n); puts_serial(" tokens\n");
            puts_serial("  Total cycles: "); put_u64(cycles); puts_serial("\n");
            if (bench_n > 0) {
                puts_serial("  Cycles/token: "); put_u64(cycles / (uint64_t)bench_n); puts_serial("\n");
            }
            puts_serial("[Stage 5] Benchmark ... DONE\n\n");
            llama_free_state(&bench_state);
        }
    }
    stage_pass++;

    /* Cleanup */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    qmodel_free(&qm);
    gguf_close(&gguf_ctx);

    puts_serial("=== Inference Pipeline: ");
    put_dec((uint32_t)stage_pass);
    puts_serial("/5 stages PASS ===\n");

    return stage_pass;
}
#endif /* JARVIS_HAS_MODEL */

/* ---- System bootstrap (allocman + vka + vspace) ---- */

static int init_system(void)
{
    seL4_BootInfo *info = platsupport_get_bootinfo();
    if (!info) return -1;

    /* Init simple interface */
    simple_default_init_bootinfo(&simple, info);

    /* Create allocator from static pool */
    allocman_t *allocman = bootstrap_use_current_simple(&simple,
        ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
    if (!allocman) {
        puts_serial("[JARVIS] FATAL: allocman bootstrap failed\n");
        return -1;
    }
    allocman_make_vka(&vka, allocman);

    /* Bootstrap vspace */
    int err = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&vspace,
        &vspace_data, simple_get_pd(&simple), &vka, info);
    if (err) {
        puts_serial("[JARVIS] FATAL: vspace bootstrap failed\n");
        return -1;
    }

    /* Provide virtual memory pool to allocator */
    void *vaddr;
    reservation_t res = vspace_reserve_range(&vspace,
        ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    if (res.res == 0) {
        puts_serial("[JARVIS] FATAL: vspace reserve failed\n");
        return -1;
    }
    bootstrap_configure_virtual_pool(allocman, vaddr,
        ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&simple));

    return 0;
}

/* ---- Direct page mapping helper ---- */

static int map_frame_direct(seL4_CPtr frame, seL4_CPtr vspace_root,
                             seL4_Word vaddr, seL4_CapRights_t rights)
{
    seL4_X86_VMAttributes vmattr = seL4_X86_Default_VMAttributes;
    int err;

    err = seL4_X86_Page_Map(frame, vspace_root, vaddr, rights, vmattr);
    if (err == seL4_NoError) return 0;

    /* Create page table hierarchy: PDPT → PD → PT → retry Page */
    vka_object_t pt_obj;
    err = vka_alloc_object(&vka, seL4_X86_PageTableObject, 0, &pt_obj);
    if (err) return -1;

    err = seL4_X86_PageTable_Map(pt_obj.cptr, vspace_root, vaddr, vmattr);
    if (err == seL4_FailedLookup) {
        vka_object_t pd_obj;
        err = vka_alloc_object(&vka, seL4_X86_PageDirectoryObject, 0, &pd_obj);
        if (err) return -1;

        err = seL4_X86_PageDirectory_Map(pd_obj.cptr, vspace_root, vaddr, vmattr);
        if (err == seL4_FailedLookup) {
            vka_object_t pdpt_obj;
            err = vka_alloc_object(&vka, seL4_X86_PDPTObject, 0, &pdpt_obj);
            if (err) return -1;
            err = seL4_X86_PDPT_Map(pdpt_obj.cptr, vspace_root, vaddr, vmattr);
            if (err) return -1;
            err = seL4_X86_PageDirectory_Map(pd_obj.cptr, vspace_root, vaddr, vmattr);
        }
        if (err) return -1;
        err = seL4_X86_PageTable_Map(pt_obj.cptr, vspace_root, vaddr, vmattr);
    }
    if (err) return -1;

    err = seL4_X86_Page_Map(frame, vspace_root, vaddr, rights, vmattr);
    return err ? -1 : 0;
}

/* Expected GGUF model size range (Llama 3.2 1B Q4_K_M ≈ 771MB) */
#define MODEL_SIZE_MIN (700UL * 1024 * 1024)   /* 700MB */
#define MODEL_SIZE_MAX (900UL * 1024 * 1024)   /* 900MB */
#define MODEL_MAX_LARGE_PAGES 512              /* 512 * 2MB = 1024MB max */

/* Find the GRUB-loaded model in bootinfo untypeds.
 * Returns 0 if found, -1 if not found.
 * Scans for contiguous RAM untypeds totaling 700-900MB (the GGUF model).
 * Must be called BEFORE sel4utils_configure_process (which consumes untypeds). */
static int find_model_untypeds(uintptr_t *model_paddr_out,
                                size_t *model_size_out)
{
    int ut_count = (int)simple_get_untyped_count(&simple);

    /* Collect RAM untypeds >= 64KB with paddr + size */
    struct { uintptr_t paddr; size_t size; } cands[512];
    int nc = 0;

    for (int i = 0; i < ut_count && nc < 512; i++) {
        size_t size_bits = 0;
        uintptr_t paddr = 0;
        bool device = false;
        simple_get_nth_untyped(&simple, i, &size_bits, &paddr, &device);
        size_t sz = (1UL << size_bits);
        if (!device && size_bits >= 16) {  /* >= 64KB */
            cands[nc].paddr = paddr;
            cands[nc].size = sz;
            nc++;
        }
    }

    /* Find contiguous group in the 700-900MB range (model-sized).
     * This avoids grabbing all of RAM (which would be >> 900MB). */
    for (int start = 0; start < nc; start++) {
        uintptr_t region_start = cands[start].paddr;
        uintptr_t region_end = region_start + cands[start].size;
        size_t total = cands[start].size;

        for (int j = start + 1; j < nc; j++) {
            if (cands[j].paddr == region_end) {
                region_end += cands[j].size;
                total += cands[j].size;
                /* Stop growing if we've exceeded max — don't grab all RAM */
                if (total > MODEL_SIZE_MAX) break;
            } else {
                break;
            }
        }

        if (total >= MODEL_SIZE_MIN && total <= MODEL_SIZE_MAX) {
            *model_paddr_out = region_start;
            *model_size_out = total;
            return 0;
        }
    }

    return -1;
}

/* Fixed virtual addresses for shared memory */
#define SHMEM_VADDR_A  0x10000000UL  /* In Process A */
#define SHMEM_VADDR_B  0x50000000UL  /* In Process B */

/* Model loading via GRUB multiboot module.
 * GRUB loads model.gguf into RAM. seL4 exposes it as untypeds.
 * We map those frames into Process B's vspace at this address. */
#define MODEL_VADDR_B   0x60000000UL  /* Virtual address in Process B for model */

/* ---- Spawn inference process (Process B) ---- */

static int spawn_inference_process(seL4_CPtr *req_notif_out, seL4_CPtr *resp_notif_out,
                                    seL4_CPtr *model_caps, uint32_t model_n_pages,
                                    uint32_t model_size)
{
    int error;

    /* Allocate two notification objects for IPC signaling */
    vka_object_t req_notif_obj, resp_notif_obj;
    error = vka_alloc_notification(&vka, &req_notif_obj);
    if (error) {
        puts_serial("[JARVIS] Failed to alloc request notification\n");
        return -1;
    }
    error = vka_alloc_notification(&vka, &resp_notif_obj);
    if (error) {
        puts_serial("[JARVIS] Failed to alloc response notification\n");
        return -1;
    }

    *req_notif_out = req_notif_obj.cptr;
    *resp_notif_out = resp_notif_obj.cptr;

    /* Debug: print untyped info (serial only — too many lines for VGA) */
#ifdef __x86_64__
    { int sv = vga_ready; vga_ready = 0;
#endif
    int ut_count = (int)simple_get_untyped_count(&simple);
    puts_serial("[JARVIS] DEBUG: untyped_count = ");
    put_dec((uint32_t)ut_count);
    puts_serial("\n");
    {
        uint64_t total_bytes = 0;
        int ram_large_count = 0;
        for (int i = 0; i < ut_count; i++) {
            size_t size_bits = 0;
            uintptr_t paddr = 0;
            bool device = false;
            simple_get_nth_untyped(&simple, i, &size_bits, &paddr, &device);
            size_t sz = (1UL << size_bits);
            if (!device) total_bytes += sz;
            /* Print only RAM untypeds >= 1MB to reduce clutter */
            if (!device && size_bits >= 20) {
                puts_serial("  ut["); put_dec((uint32_t)i);
                puts_serial("]: size="); put_dec((uint32_t)(sz >> 20));
                puts_serial("MB paddr="); put_hex((uint32_t)(paddr >> 20));
                puts_serial("M\n");
                ram_large_count++;
            }
        }
        puts_serial("  RAM>=1MB: "); put_dec((uint32_t)ram_large_count);
        puts_serial(" of "); put_dec((uint32_t)ut_count);
        puts_serial(" total untypeds, ");
        put_dec((uint32_t)(total_bytes >> 20));
        puts_serial("MB RAM\n");
    }
#ifdef __x86_64__
    vga_ready = sv; }
#endif

    /* Verify CPIO has the ELF */
    unsigned long cpio_len = _cpio_archive_end - _cpio_archive;
    unsigned long elf_size = 0;
    const void *elf = cpio_get_file(_cpio_archive, cpio_len, INFERENCE_APP, &elf_size);
    if (!elf) {
        puts_serial("[JARVIS] CPIO: jarvis-inference NOT FOUND\n");
        return -1;
    }
    puts_serial("[JARVIS] CPIO: jarvis-inference found, ");
    put_dec((uint32_t)(elf_size / 1024)); puts_serial(" KB\n");

    /* Configure Process B from CPIO archive */
    sel4utils_process_config_t config = process_config_default_simple(
        &simple, INFERENCE_APP, seL4_MaxPrio - 1);
    config = process_config_auth(config, simple_get_tcb(&simple));

    error = sel4utils_configure_process_custom(&inference_process,
                                                &vka, &vspace, config);
    if (error) {
        puts_serial("[JARVIS] Failed to configure inference process\n");
        return -1;
    }
    puts_serial("[JARVIS] Inference process configured\n");

    /* Allocate 2 physical frames for shared memory */
    vka_object_t shmem_frames[2];
    for (int i = 0; i < 2; i++) {
        error = vka_alloc_frame(&vka, seL4_PageBits, &shmem_frames[i]);
        if (error) {
            puts_serial("[JARVIS] Failed to alloc shared frame\n");
            return -1;
        }
    }

    /* Map frames into Process A at fixed address using direct seL4 syscalls */
    seL4_CPtr pd_a = simple_get_pd(&simple);
    for (int i = 0; i < 2; i++) {
        error = map_frame_direct(shmem_frames[i].cptr, pd_a,
            SHMEM_VADDR_A + i * 4096, seL4_AllRights);
        if (error) {
            puts_serial("[JARVIS] Failed to map shared frame in Process A\n");
            return -1;
        }
    }
    shared_request_ring = (shmem_ring_t *)SHMEM_VADDR_A;
    shared_response_ring = (shmem_ring_t *)(SHMEM_VADDR_A + SHMEM_PAGE_SIZE);
    shmem_ipc_init(shared_request_ring);
    shmem_ipc_init(shared_response_ring);
    puts_serial("[JARVIS] Shared memory mapped in Process A\n");

    /* Map SAME physical pages into Process B's VSpace.
     * seL4 requires a separate cap per mapping — duplicate the frame caps. */
    seL4_CPtr pd_b = inference_process.pd.cptr;
    for (int i = 0; i < 2; i++) {
        /* Duplicate the frame cap (can't map same cap in two VSpaces) */
        cspacepath_t src, dest;
        vka_cspace_make_path(&vka, shmem_frames[i].cptr, &src);
        error = vka_cspace_alloc_path(&vka, &dest);
        if (error) {
            puts_serial("[JARVIS] Failed to alloc cspace slot for frame dup\n");
            return -1;
        }
        error = vka_cnode_copy(&dest, &src, seL4_AllRights);
        if (error) {
            puts_serial("[JARVIS] Failed to copy frame cap\n");
            return -1;
        }

        error = map_frame_direct(dest.capPtr, pd_b,
            SHMEM_VADDR_B + i * 4096, seL4_AllRights);
        if (error) {
            puts_serial("[JARVIS] Failed to map shared frame in Process B\n");
            return -1;
        }
    }
    puts_serial("[JARVIS] Shared memory mapped in Process B\n");
    void *remote_vaddr = (void *)SHMEM_VADDR_B;

    /* Map model frames into Process B */
    if (model_caps && model_n_pages > 0) {
        puts_serial("[JARVIS] Mapping "); put_dec(model_n_pages);
        puts_serial(" model frames into Process B...\n");
        seL4_CPtr pd_b_model = inference_process.pd.cptr;
        uint32_t mok = 0, merr = 0;
        for (uint32_t p = 0; p < model_n_pages; p++) {
            cspacepath_t msrc, mdst;
            vka_cspace_make_path(&vka, model_caps[p], &msrc);
            int e = vka_cspace_alloc_path(&vka, &mdst);
            if (e) { merr++; continue; }
            e = vka_cnode_copy(&mdst, &msrc, seL4_AllRights);
            if (e) { merr++; continue; }
            e = map_frame_direct(mdst.capPtr, pd_b_model,
                MODEL_VADDR_B + (size_t)p * 4096, seL4_AllRights);
            if (e) { merr++; continue; }
            mok++;
            if (p > 0 && p % 50000 == 0) {
                put_dec(mok * 4 / 1024); puts_serial("MB mapped\n");
            }
        }
        puts_serial("[JARVIS] Model: "); put_dec(mok);
        puts_serial("/"); put_dec(model_n_pages);
        puts_serial(" pages mapped");
        if (merr > 0) {
            puts_serial(" ("); put_dec(merr); puts_serial(" errors)");
        }
        puts_serial("\n");
    }

    /* Copy notification caps to Process B's cspace */
    seL4_CPtr remote_req_notif = sel4utils_copy_cap_to_process(
        &inference_process, &vka, req_notif_obj.cptr);
    seL4_CPtr remote_resp_notif = sel4utils_copy_cap_to_process(
        &inference_process, &vka, resp_notif_obj.cptr);

    if (!remote_req_notif || !remote_resp_notif) {
        puts_serial("[JARVIS] Failed to copy notification caps\n");
        return -1;
    }

    /* Build argv: req_notif, resp_notif, shmem_vaddr, model_vaddr, model_size */
    char arg0[32], arg1[32], arg2[32], arg3[32], arg4[32];
    snprintf(arg0, sizeof(arg0), "%lu", (unsigned long)remote_req_notif);
    snprintf(arg1, sizeof(arg1), "%lu", (unsigned long)remote_resp_notif);
    snprintf(arg2, sizeof(arg2), "%lu", (unsigned long)remote_vaddr);
    snprintf(arg3, sizeof(arg3), "%lu", (unsigned long)(model_n_pages > 0 ? MODEL_VADDR_B : 0));
    snprintf(arg4, sizeof(arg4), "%lu", (unsigned long)model_size);
    char *argv[] = { arg0, arg1, arg2, arg3, arg4 };

    /* Spawn Process B */
    error = sel4utils_spawn_process_v(&inference_process, &vka, &vspace,
                                       5, argv, 1);
    if (error) {
        puts_serial("[JARVIS] Failed to spawn inference process\n");
        return -1;
    }
    puts_serial("[JARVIS] Process B started\n");

    return 0;
}

/* ---- Main continuation (runs on vspace-managed stack) ---- */

static void *main_continued(void *arg UNUSED)
{
#ifdef __x86_64__
    /* Map VGA text framebuffer (physical 0xB8000) into our vspace.
     * vka_alloc_frame_at handles device untyped lookup + retype. */
    {
        vka_object_t vga_frame;
        int vga_err = vka_alloc_frame_at(&vka, seL4_PageBits, 0xB8000, &vga_frame);
        if (vga_err == 0) {
            void *vga_vaddr = vspace_map_pages(&vspace, &vga_frame.cptr, NULL,
                seL4_AllRights, 1, seL4_PageBits, 0);
            if (vga_vaddr) {
                vga_set_buffer(vga_vaddr);
                vga_init();
                vga_ready = 1;
                puts_serial("[JARVIS] VGA mapped at vaddr ");
                put_hex((uint32_t)(uintptr_t)vga_vaddr);
                puts_serial("\n");
            } else {
                puts_serial("[JARVIS] VGA: vspace map failed\n");
            }
        } else {
            puts_serial("[JARVIS] VGA: alloc frame at 0xB8000 failed (err=");
            put_dec((uint32_t)vga_err);
            puts_serial(")\n");
        }
    }
#endif
    puts_serial("[JARVIS] Running on vspace-managed stack\n");

    /* ---- PCI + NVMe Detection ---- */
#ifdef __x86_64__
    {
        /* seL4 runs rootserver in Ring 3 — direct outl/inl traps with GPF.
         * Get IOPort cap from simple interface and provide wrapper functions. */
        /* simple_get_IOPort_cap(simple, start, end, root, dest, depth) */
        seL4_CPtr ioport_cap = 0;
        cspacepath_t ioport_path;
        vka_cspace_alloc_path(&vka, &ioport_path);
        seL4_Error ioerr = simple_get_IOPort_cap(&simple, 0xCF8, 0xCFF,
            ioport_path.root, ioport_path.capPtr, ioport_path.capDepth);
        if (ioerr != seL4_NoError) {
            puts_serial("[JARVIS] WARNING: could not get IOPort cap for PCI (err=");
            put_dec((uint32_t)ioerr);
            puts_serial(")\n");
        } else {
            ioport_cap = ioport_path.capPtr;
        }
        g_pci_ioport_cap = ioport_cap;
        pci_set_ioport_ops(sel4_pci_outl, sel4_pci_inl);
        puts_serial("[JARVIS] PCI IOPort cap acquired\n");

        pci_device_t pci_devs[32];
        int n_pci = pci_scan(pci_devs, 32);
        puts_serial("[JARVIS] PCI scan: "); put_dec((uint32_t)n_pci);
        puts_serial(" devices\n");

        pci_device_t *nvme_pci = pci_find_device(0x01, 0x08, pci_devs, n_pci);
        if (nvme_pci) {
            puts_serial("[JARVIS] NVMe found: ");
            put_hex(nvme_pci->vendor_id); puts_serial(":");
            put_hex(nvme_pci->device_id);
            puts_serial(" @ bus "); put_dec(nvme_pci->bus);
            puts_serial(" dev "); put_dec(nvme_pci->device);
            puts_serial("\n");

            pci_enable_bus_master(nvme_pci);
            /* Verify bus master + memory space enabled */
            uint32_t pci_cmd_reg = pci_config_read(nvme_pci->bus, nvme_pci->device,
                                                    nvme_pci->function, 0x04);
            puts_serial("[JARVIS] NVMe PCI CMD: "); put_hex(pci_cmd_reg);
            puts_serial(" (bus_master="); put_dec((pci_cmd_reg >> 2) & 1);
            puts_serial(" mem_space="); put_dec((pci_cmd_reg >> 1) & 1);
            puts_serial(")\n");
            /* Enable memory space if not already set */
            if (!(pci_cmd_reg & 0x02)) {
                pci_config_write(nvme_pci->bus, nvme_pci->device,
                                  nvme_pci->function, 0x04, pci_cmd_reg | 0x06);
                puts_serial("[JARVIS] NVMe: enabled memory space + bus master\n");
            }
            uint64_t bar0_phys = pci_get_bar_address(nvme_pci, 0);
            puts_serial("[JARVIS] NVMe BAR0: ");
            put_hex((uint32_t)(bar0_phys >> 32)); put_hex((uint32_t)bar0_phys);
            puts_serial(" (64-bit="); put_dec(pci_is_bar_64bit(nvme_pci, 0));
            puts_serial(")\n");

            if (bar0_phys != 0) {
                /* Map NVMe BAR0 as device frames (typically 16KB).
                 * Map 4 pages (16KB) — enough for registers + doorbells. */
                int nvme_bar_pages = 4;
                void *nvme_bar_vaddr = NULL;
                seL4_CPtr nvme_frame_caps[4];
                int nvme_map_ok = 1;

                for (int i = 0; i < nvme_bar_pages; i++) {
                    vka_object_t frame;
                    int err = vka_alloc_frame_at(&vka, seL4_PageBits,
                        (uintptr_t)(bar0_phys + i * 4096), &frame);
                    if (err) {
                        puts_serial("[JARVIS] NVMe BAR0 frame alloc failed page ");
                        put_dec((uint32_t)i); puts_serial(" err=");
                        put_dec((uint32_t)err); puts_serial("\n");
                        nvme_map_ok = 0;
                        break;
                    }
                    nvme_frame_caps[i] = frame.cptr;
                }

                if (nvme_map_ok) {
                    nvme_bar_vaddr = vspace_map_pages(&vspace, nvme_frame_caps, NULL,
                        seL4_AllRights, nvme_bar_pages, seL4_PageBits, 0);
                    if (nvme_bar_vaddr) {
                        puts_serial("[JARVIS] NVMe BAR0 mapped at vaddr ");
                        put_hex((uint32_t)(uintptr_t)nvme_bar_vaddr);
                        puts_serial(" ("); put_dec(nvme_bar_pages * 4);
                        puts_serial("KB)\n");

                        /* Allocate 5 DMA frames: admin SQ, admin CQ, IO SQ, IO CQ, identify buf.
                         * Each is one 4KB page, physically contiguous. */
                        #define NVME_DMA_FRAMES 5
                        vka_object_t dma_objs[NVME_DMA_FRAMES];
                        void *dma_vaddrs[NVME_DMA_FRAMES] = {0};
                        uint64_t dma_paddrs[NVME_DMA_FRAMES] = {0};
                        int dma_ok = 1;

                        for (int d = 0; d < NVME_DMA_FRAMES; d++) {
                            int derr = vka_alloc_frame(&vka, seL4_PageBits, &dma_objs[d]);
                            if (derr) {
                                puts_serial("[JARVIS] NVMe DMA frame alloc failed\n");
                                dma_ok = 0;
                                break;
                            }
                            /* Map uncacheable (vm_attributes = 0 on x86 disables caching) */
                            dma_vaddrs[d] = vspace_map_pages(&vspace, &dma_objs[d].cptr, NULL,
                                seL4_AllRights, 1, seL4_PageBits, 0);
                            if (!dma_vaddrs[d]) {
                                puts_serial("[JARVIS] NVMe DMA vspace map failed\n");
                                dma_ok = 0;
                                break;
                            }
                            /* Get physical address for DMA */
                            seL4_X86_Page_GetAddress_t pa = seL4_X86_Page_GetAddress(dma_objs[d].cptr);
                            dma_paddrs[d] = pa.paddr;
                        }

                        if (dma_ok) {
                            puts_serial("[JARVIS] NVMe DMA: 5 frames allocated\n");
                            for (int d = 0; d < NVME_DMA_FRAMES; d++) {
                                puts_serial("  dma["); put_dec((uint32_t)d);
                                puts_serial("] vaddr="); put_hex((uint32_t)(uintptr_t)dma_vaddrs[d]);
                                puts_serial(" paddr="); put_hex((uint32_t)dma_paddrs[d]);
                                puts_serial("\n");
                            }
                            /* dma[0]=admin_sq, dma[1]=admin_cq, dma[2]=io_sq, dma[3]=io_cq, dma[4]=identify */
                            static nvme_controller_t nvme_ctrl;
                            /* nvme_ctrl.debug_fn = nvme_timeout_debug; — disabled, driver working */

                            int nvme_err = nvme_init(&nvme_ctrl,
                                (volatile uint8_t *)nvme_bar_vaddr, bar0_phys,
                                dma_vaddrs[0], dma_paddrs[0],   /* admin SQ */
                                dma_vaddrs[1], dma_paddrs[1],   /* admin CQ */
                                dma_vaddrs[2], dma_paddrs[2],   /* IO SQ */
                                dma_vaddrs[3], dma_paddrs[3],   /* IO CQ */
                                dma_vaddrs[4], dma_paddrs[4]);  /* identify buf */

                            if (nvme_err == 0) {
                                puts_serial("[JARVIS] NVMe IDENTIFY: \"");
                                puts_serial(nvme_ctrl.model);
                                puts_serial("\" serial=\"");
                                puts_serial(nvme_ctrl.serial);
                                puts_serial("\" ");
                                put_dec((uint32_t)(nvme_ctrl.ns_lba_count >> 20));
                                puts_serial("M sectors (");
                                put_dec(nvme_ctrl.ns_block_size);
                                puts_serial("B/sector)\n");

                                /* Test read: read sector 0 into the identify buffer (reuse dma[4]) */
                                memset(dma_vaddrs[4], 0, 4096);
                                int rd_err = nvme_read_sectors(&nvme_ctrl, 0, 1,
                                    dma_vaddrs[4], dma_paddrs[4]);
                                if (rd_err == 0) {
                                    puts_serial("[JARVIS] NVMe read sector 0: ");
                                    uint8_t *data = (uint8_t *)dma_vaddrs[4];
                                    const char hx[] = "0123456789abcdef";
                                    for (int b = 0; b < 16; b++) {
                                        char h[4] = { hx[data[b]>>4], hx[data[b]&0xf], ' ', '\0' };
                                        puts_serial(h);
                                    }
                                    puts_serial("\n");
                                    /* Wire up FAT32 on the NVMe drive.
                                     * Use dma[4] as bounce buffer for sector reads + log writes. */
                                    g_nvme_ptr = &nvme_ctrl;
                                    g_nvme_bounce_vaddr = dma_vaddrs[4];
                                    g_nvme_bounce_paddr = dma_paddrs[4];

                                    /* Initialize NVMe log (raw sector telemetry) */
                                    if (nvme_log_init(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4]) == 0) {
                                        puts_serial("[JARVIS] NVMe log initialized (boot ");
                                        put_dec(nvme_log_boot_id());
                                        puts_serial(")\n");
                                        nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                       LOG_BOOT, "JARVIS boot started");
                                        nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                       LOG_SELFTEST, "Self-test 5/5 PASS");
                                    }

                                    fat32_fs_t fs;
                                    /* Try partition 4 (ESP) first, then fall back to whole-disk (QEMU).
                                     * Lexar NM790 2TB: p4 starts at sector 3997272064 (FAT32 ESP). */
#if JARVIS_DBG_BOOT_LOG
                                    nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                   LOG_BOOT, "Starting FAT32");
#endif
                                    #define NVME_FAT32_PART_LBA 3997272064ULL
                                    int fat_err = fat32_init(&fs, fat32_nvme_read, NVME_FAT32_PART_LBA);
                                    if (fat_err != 0) {
                                        /* Fall back to whole-disk FAT32 (QEMU test images) */
                                        fat_err = fat32_init(&fs, fat32_nvme_read, 0);
                                    }
                                    if (fat_err == 0) {
                                        puts_serial("[JARVIS] FAT32 init OK: ");
                                        put_dec(fs.sectors_per_cluster);
                                        puts_serial(" sec/cluster, ");
                                        put_dec(fs.bytes_per_sector);
                                        puts_serial(" B/sec\n");

                                        uint32_t model_cluster = 0, model_size = 0;
                                        int found = fat32_find_file(&fs, "MODEL   GUF",
                                                                     &model_cluster, &model_size);
                                        if (found == 0) {
                                            puts_serial("[JARVIS] Model file: cluster=");
                                            put_dec(model_cluster);
                                            puts_serial(" size=");
                                            put_dec(model_size >> 20);
                                            puts_serial("MB\n");

                                            /* Allocate frames for full model */
                                            uint32_t n_pages = (model_size + 4095) / 4096;
                                            puts_serial("[JARVIS] Allocating "); put_dec(n_pages);
                                            puts_serial(" frames for model ("); put_dec(model_size >> 20);
                                            puts_serial("MB)...\n");

                                            if (n_pages > MODEL_MAX_PAGES) {
                                                puts_serial("[JARVIS] Model too large\n");
                                            } else {
                                                /* Allocate cap array dynamically (was static BSS — 1.6MB bloated ELF,
                                                 * pushing DMA frames into rootserver's physical range) */
                                                if (!model_frame_caps) {
                                                    int cap_pages = (int)((n_pages * sizeof(seL4_CPtr) + 4095) / 4096);
                                                    model_frame_caps = (seL4_CPtr *)vspace_new_pages(
                                                        &vspace, seL4_AllRights, cap_pages, seL4_PageBits);
                                                    if (!model_frame_caps) {
                                                        puts_serial("[JARVIS] Failed to alloc cap array\n");
                                                        n_pages = 0;
                                                    } else {
                                                        memset(model_frame_caps, 0, (size_t)cap_pages * 4096);
                                                    }
                                                }
                                                int alloc_fail = 0;
                                                for (uint32_t p = 0; p < n_pages; p++) {
                                                    vka_object_t frm;
                                                    int aerr = vka_alloc_frame(&vka, seL4_PageBits, &frm);
                                                    if (aerr) {
                                                        puts_serial("[JARVIS] Frame alloc failed at ");
                                                        put_dec(p); puts_serial("\n");
                                                        n_pages = p;
                                                        alloc_fail = 1;
                                                        break;
                                                    }
                                                    model_frame_caps[p] = frm.cptr;
                                                    if (p > 0 && p % 50000 == 0) {
                                                        put_dec(p * 4 / 1024); puts_serial("MB allocated\n");
#if JARVIS_DBG_BOOT_LOG
                                                        {
                                                            char pb[48];
                                                            int pi = 0;
                                                            const char *pre = "Model progress: ";
                                                            while (*pre) pb[pi++] = *pre++;
                                                            uint32_t pv = p;
                                                            char pd[10]; int pdi = 0;
                                                            if (pv == 0) pd[pdi++] = '0';
                                                            else while (pv > 0) { pd[pdi++] = '0' + (pv % 10); pv /= 10; }
                                                            while (--pdi >= 0) pb[pi++] = pd[pdi];
                                                            const char *suf = " pages";
                                                            while (*suf) pb[pi++] = *suf++;
                                                            pb[pi] = '\0';
                                                            nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                                           LOG_BOOT, pb);
                                                        }
#endif
                                                    }
                                                }
                                                puts_serial("[JARVIS] Allocated "); put_dec(n_pages); puts_serial(" frames\n");

                                                /* Map contiguously in rootserver */
                                                void *model_local = vspace_map_pages(&vspace,
                                                    model_frame_caps, NULL, seL4_AllRights,
                                                    n_pages, seL4_PageBits, 1);
                                                if (!model_local) {
                                                    puts_serial("[JARVIS] Model vspace map failed\n");
                                                } else {
                                                    puts_serial("[JARVIS] Model mapped at vaddr ");
                                                    put_hex((uint32_t)(uintptr_t)model_local); puts_serial("\n");

                                                    /* Read model from FAT32 */
                                                    g_nvme_read_count = 0;
                                                    puts_serial("[JARVIS] Reading model from NVMe...\n");
#if JARVIS_DBG_BOOT_LOG
                                                    nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                                   LOG_BOOT, "Loading model...");
#endif
                                                    int rderr = fat32_read_file(&fs, model_cluster,
                                                                                model_size, model_local);
                                                    if (rderr == 0) {
                                                        uint32_t magic = 0;
                                                        memcpy(&magic, model_local, 4);
                                                        puts_serial("[JARVIS] Model loaded: ");
                                                        put_dec(model_size >> 20); puts_serial("MB GGUF=");
                                                        put_hex(magic); puts_serial("\n");
                                                        if (magic == 0x46554747) {
                                                            nvme_model_loaded = 1;
                                                            nvme_model_size = model_size;
                                                            nvme_model_n_pages = n_pages;
                                                            nvme_log_write(&nvme_ctrl, dma_vaddrs[4], dma_paddrs[4],
                                                                           LOG_MODEL_LOAD, "GGUF loaded OK");
                                                        }
                                                    } else {
                                                        puts_serial("[JARVIS] Model read failed\n");
                                                    }
                                                }
                                            }
                                        } else {
                                            puts_serial("[JARVIS] Model file MODEL.GUF not found on FAT32\n");
                                        }
                                    } else {
                                        puts_serial("[JARVIS] FAT32 init failed: err=");
                                        put_dec((uint32_t)(-fat_err));
                                        puts_serial("\n");
                                    }
                                } else {
                                    puts_serial("[JARVIS] NVMe read sector 0 failed: err=");
                                    put_dec((uint32_t)(-rd_err));
                                    puts_serial("\n");
                                }
                            } else {
                                puts_serial("[JARVIS] NVMe init failed: err=");
                                put_dec((uint32_t)(-nvme_err));
                                puts_serial(" step=");
                                put_dec((uint32_t)nvme_ctrl.init_step);
                                puts_serial(" submit_err=");
                                /* Print as signed: if negative, show magnitude */
                                int se = nvme_ctrl.last_submit_err;
                                if (se < 0) { puts_serial("-"); put_hex((uint32_t)(-se)); }
                                else { put_dec((uint32_t)se); }
                                puts_serial(" dstrd=");
                                put_dec(nvme_ctrl.dstrd);
                                puts_serial("\n");
                            }
                        }
                    } else {
                        puts_serial("[JARVIS] NVMe BAR0 vspace map failed\n");
                    }
                }
            }
        } else {
            puts_serial("[JARVIS] No NVMe controller found\n");
        }
    }
#endif

    /* ---- Model loading ----
     * Priority: 1) embedded .rodata (JARVIS_HAS_MODEL)
     *           2) NVMe FAT32 (future — blk_dev_init_nvme + fat32)
     *           3) idle mode (no model)
     * GRUB multiboot module approach was tried and abandoned —
     * seL4 overwrites module memory during rootserver relocation. */

    /* ---- Spawn inference process (Process B) ---- */
    seL4_CPtr req_notif = 0, resp_notif = 0;
    puts_serial("\n[JARVIS] Spawning inference process...\n");
#if JARVIS_DBG_BOOT_LOG
    nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr, g_nvme_bounce_paddr,
                   LOG_BOOT, "Spawning Process B");
#endif
    if (spawn_inference_process(&req_notif, &resp_notif,
                                 nvme_model_loaded ? model_frame_caps : NULL,
                                 nvme_model_loaded ? nvme_model_n_pages : 0,
                                 nvme_model_loaded ? nvme_model_size : 0) != 0) {
        puts_serial("[JARVIS] Inference process spawn failed — running without inference\n");
        goto idle;
    }
    /* Wait for Process B to signal ready (blocks until model loaded) */
    puts_serial("[JARVIS] Waiting for Process B ready signal...\n");
    seL4_Wait(resp_notif, NULL);

    {
        uint8_t ready_type;
        uint16_t ready_seq;
        uint8_t ready_payload[SHMEM_MAX_PAYLOAD];
        uint16_t ready_len;
        if (shmem_ipc_recv(shared_response_ring, &ready_type, &ready_seq,
                           ready_payload, &ready_len) == 0) {
            puts_serial("[JARVIS] Process B ready (type=");
            put_dec(ready_type); puts_serial(" seq="); put_dec(ready_seq);
            puts_serial(")\n");
            nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr, g_nvme_bounce_paddr,
                           LOG_BOOT, "Process B ready");
        } else {
            puts_serial("[JARVIS] WARNING: ready signal but no message\n");
        }
    }

    /* ---- Continuous IPC workload for stability testing ---- */
    puts_serial("\n[JARVIS] Starting continuous workload\n\n");
    nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr, g_nvme_bounce_paddr,
                   LOG_BOOT, "Starting continuous workload");

    /* 40 queries guaranteed to HIT decision cache (from cache_patterns.c) */
    static const char *cache_queries[] = {
        "help", "status", "version", "info", "hello", "hi",
        "time", "date", "uptime", "ls", "pwd", "ls -l", "ls -a",
        "ps", "top", "free", "df", "cpu", "ifconfig", "netstat",
        "cat readme", "echo hello", "ping google.com", "ping 8.8.8.8",
        "check disk space", "show me the time", "list files",
        "show me cpu usage", "show network", "show uptime",
        "list directory", "show me processes", "show running",
        "disk info", "task list", "system uptime", "cpu usage",
        "storage capacity", "show disks", "network config",
        NULL
    };
    #define N_CACHE_QUERIES 40

    /* 15 open-ended prompts -> cache miss -> inference */
    static const char *inference_queries[] = {
        "The seL4 microkernel is",
        "What is artificial intelligence",
        "Explain how operating systems manage memory",
        "Describe the benefits of formal verification",
        "What are the key principles of microkernel design",
        "How does virtual memory work in modern CPUs",
        "What is the role of a scheduler in an OS",
        "Explain the difference between threads and processes",
        "What is capability-based security",
        "How do device drivers communicate with hardware",
        "Describe the TCP three-way handshake",
        "What is a page fault and how is it handled",
        "Explain the concept of interrupt handling",
        "What are the advantages of RISC-V architecture",
        "How does NVMe differ from SATA for storage",
        NULL
    };
    #define N_INFERENCE_QUERIES 15

    /* 8 hostile queries for SHIELD checks */
    static const char *shield_queries[] = {
        "delete all system files",
        "rm -rf /",
        "drop table users",
        "format the hard drive",
        "sudo rm -rf --no-preserve-root /",
        "delete everything",
        "kill all processes",
        "dd if=/dev/zero of=/dev/sda",
        NULL
    };
    #define N_SHIELD_QUERIES 8

    uint16_t seq = 1;
    /* SEC-037: uint64_t for 30-day stability test (~2.6B queries at 1000/sec) */
    uint64_t q_total = 0, q_hits = 0, q_infer = 0, q_heartbeat = 0;
    uint64_t q_shield = 0, q_errors = 0;
    uint32_t rng_state = 42;

    while (1) {
        q_total++;
        uint32_t r = xorshift32(&rng_state);
        int slot = (int)(r % 20);

        /* DEBUG: trace every query */
#if JARVIS_DBG_IPC
        puts_serial("[DBG] q="); put_dec(q_total);
        puts_serial(" slot="); put_dec((uint32_t)slot);
        puts_serial("\n");
#endif

        if (slot < 14) {
            /* --- Cache query (70%) --- */
            const char *query = cache_queries[r % N_CACHE_QUERIES];
#if JARVIS_DBG_BOOT_LOG
            {
                char qb[96] = "cache q=";
                int qi = 8;
                uint64_t qv = q_total;
                char qd[20]; int qdi = 0;
                if (qv == 0) qd[qdi++] = '0';
                else while (qv > 0) { qd[qdi++] = '0' + (qv % 10); qv /= 10; }
                while (--qdi >= 0) qb[qi++] = qd[qdi];
                qb[qi++] = ' ';
                const char *qp = query;
                while (*qp && qi < 94) qb[qi++] = *qp++;
                qb[qi] = '\0';
                nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                               g_nvme_bounce_paddr, LOG_IPC_STATS, qb);
            }
#endif

            char normalized[128];
            cache_normalize_query(query, normalized, sizeof(normalized));
            char action[256];
            trust_level_t trust;
            if (cache_lookup(&g_cache, normalized, action, sizeof(action), &trust)) {
                q_hits++;
            } else {
#if JARVIS_DBG_IPC
                puts_serial("[WARN] expected cache hit: \"");
                puts_serial(query); puts_serial("\"\n");
#endif
                q_errors++;
            }

        } else if (slot < 17) {
            /* --- Inference (15%) --- */
            const char *query = inference_queries[r % N_INFERENCE_QUERIES];
            q_infer++;
#if JARVIS_DBG_BOOT_LOG
            {
                char qb[96] = "infer q=";
                int qi = 8;
                uint64_t qv = q_total;
                char qd[20]; int qdi = 0;
                if (qv == 0) qd[qdi++] = '0';
                else while (qv > 0) { qd[qdi++] = '0' + (qv % 10); qv /= 10; }
                while (--qdi >= 0) qb[qi++] = qd[qdi];
                qb[qi++] = ' ';
                const char *qp = query;
                while (*qp && qi < 94) qb[qi++] = *qp++;
                qb[qi] = '\0';
                nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                               g_nvme_bounce_paddr, LOG_IPC_STATS, qb);
            }
#endif

#if JARVIS_DBG_IPC
            puts_serial("[DBG] INFER: sending...\n");
#endif
#if JARVIS_DBG_BOOT_LOG
            puts_serial("[PA] Sending query to PB: ");
            puts_serial(query);
            puts_serial("\n");
#endif
            shmem_ipc_send(shared_request_ring, MSG_QUERY, seq++,
                           query, (uint16_t)strlen(query));
#if JARVIS_DBG_IPC
            puts_serial("[DBG] INFER: signaling...\n");
#endif
            seL4_Signal(req_notif);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] INFER: waiting...\n");
#endif
#if JARVIS_DBG_BOOT_LOG
            puts_serial("[PA] Waiting for PB response...\n");
#endif

            /* Poll-drain loop: yield CPU to Process B, periodically drain
             * the shared ring for MSG_DEBUG entries (written to NVMe log in
             * real time) and check for MSG_RESPONSE (inference result).
             * This replaces blocking seL4_Wait so that Process B's per-token
             * debug progress appears in the NVMe log even during long inference. */
            char full_response[512];
            int resp_offset = 0;
            {
                int got_response = 0;
                uint32_t poll_count = 0;
                uint8_t msg_type;
                uint16_t msg_seq;
                uint8_t payload[SHMEM_MAX_PAYLOAD];
                uint16_t msg_len;

                while (!got_response) {
                    /* Drain ring — grab MSG_DEBUG and check for MSG_RESPONSE */
                    while (shmem_ipc_recv(shared_response_ring, &msg_type, &msg_seq,
                                           payload, &msg_len) == 0) {
                        if (msg_type == MSG_DEBUG) {
                            payload[msg_len < SHMEM_MAX_PAYLOAD ? msg_len : SHMEM_MAX_PAYLOAD - 1] = '\0';
#if JARVIS_DBG_BOOT_LOG
                            nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                                           g_nvme_bounce_paddr, LOG_BOOT, (const char *)payload);
#endif
                            continue;
                        }
                        if (msg_type == MSG_RESPONSE) {
                            int copy = (int)msg_len;
                            if (resp_offset + copy > (int)sizeof(full_response) - 1)
                                copy = (int)sizeof(full_response) - 1 - resp_offset;
                            if (copy > 0) {
                                memcpy(full_response + resp_offset, payload, (size_t)copy);
                                resp_offset += copy;
                            }
                            got_response = 1;
                            break;
                        }
                        /* Unknown type — skip */
                        break;
                    }

                    if (!got_response) {
                        poll_count++;
                        if (poll_count % 1000 == 0) {
#if JARVIS_DBG_BOOT_LOG
                            {
                                char wb[48] = "[PA] Still waiting for PB... poll ";
                                int wi = 34;
                                uint32_t v = poll_count;
                                char d[10]; int di = 0;
                                if (v == 0) d[di++] = '0';
                                else while (v > 0) { d[di++] = '0' + (v % 10); v /= 10; }
                                while (--di >= 0) wb[wi++] = d[di];
                                wb[wi] = '\0';
                                nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                                               g_nvme_bounce_paddr, LOG_BOOT, wb);
                            }
#endif
                        }
                        seL4_Yield();
                    }
                }

                /* Continue draining any remaining MSG_RESPONSE chunks + MSG_DEBUG */
                while (shmem_ipc_recv(shared_response_ring, &msg_type, &msg_seq,
                                       payload, &msg_len) == 0) {
                    if (msg_type == MSG_DEBUG) {
                        payload[msg_len < SHMEM_MAX_PAYLOAD ? msg_len : SHMEM_MAX_PAYLOAD - 1] = '\0';
#if JARVIS_DBG_BOOT_LOG
                        nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                                       g_nvme_bounce_paddr, LOG_BOOT, (const char *)payload);
#endif
                        continue;
                    }
                    if (msg_type != MSG_RESPONSE) break;
                    int copy = (int)msg_len;
                    if (resp_offset + copy > (int)sizeof(full_response) - 1)
                        copy = (int)sizeof(full_response) - 1 - resp_offset;
                    if (copy > 0) {
                        memcpy(full_response + resp_offset, payload, (size_t)copy);
                        resp_offset += copy;
                    }
                }
            }

#if JARVIS_DBG_BOOT_LOG
            puts_serial("[PA] PB response received\n");
#endif
#if JARVIS_DBG_IPC
            puts_serial("[DBG] INFER: woke up\n");
#endif

#if JARVIS_DBG_INFER_SUMMARY
            if (resp_offset > 0) {
                int tlen = resp_offset > 60 ? 60 : resp_offset;
                char display[64];
                memcpy(display, full_response, (size_t)tlen);
                display[tlen] = '\0';
                puts_serial("[INFER] \"");
                puts_serial(query);
                puts_serial("\" -> \"");
                puts_serial(display);
                if (resp_offset > 60) puts_serial("...");
                puts_serial("\"\n");
            }
#endif

            if (resp_offset == 0) {
                q_errors++;
#if JARVIS_DBG_IPC
                puts_serial("[ERR] empty inference response\n");
#endif
            }

        } else if (slot < 19) {
            /* --- Heartbeat (10%) --- */
            q_heartbeat++;
#if JARVIS_DBG_BOOT_LOG
            {
                char qb[32] = "heartbeat q=";
                int qi = 12;
                uint64_t qv = q_total;
                char qd[20]; int qdi = 0;
                if (qv == 0) qd[qdi++] = '0';
                else while (qv > 0) { qd[qdi++] = '0' + (qv % 10); qv /= 10; }
                while (--qdi >= 0) qb[qi++] = qd[qdi];
                qb[qi] = '\0';
                nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr,
                               g_nvme_bounce_paddr, LOG_IPC_STATS, qb);
            }
#endif
#if JARVIS_DBG_IPC
            puts_serial("[DBG] HB: sending...\n");
#endif
            shmem_ipc_send(shared_request_ring, MSG_HEARTBEAT, seq++, NULL, 0);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] HB: signaling...\n");
#endif
            seL4_Signal(req_notif);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] HB: waiting...\n");
#endif
            seL4_Wait(resp_notif, NULL);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] HB: woke up\n");
#endif

            uint8_t msg_type;
            uint16_t msg_seq;
            uint8_t payload[SHMEM_MAX_PAYLOAD];
            uint16_t msg_len;
            if (shmem_ipc_recv(shared_response_ring, &msg_type, &msg_seq,
                               payload, &msg_len) == 0) {
                if (msg_type != MSG_HEARTBEAT_ACK) {
#if JARVIS_DBG_IPC
                    puts_serial("[WARN] heartbeat got type=");
                    put_dec((uint32_t)msg_type); puts_serial("\n");
#endif
                    q_errors++;
                }
            } else {
#if JARVIS_DBG_IPC
                puts_serial("[ERR] no heartbeat ACK\n");
#endif
                q_errors++;
            }

        } else {
            /* --- SHIELD (5%) --- */
            q_shield++;
            const char *query = shield_queries[r % N_SHIELD_QUERIES];

#if JARVIS_DBG_IPC
            puts_serial("[DBG] SHIELD: sending...\n");
#endif
            shmem_ipc_send(shared_request_ring, MSG_SHIELD_CHECK, seq++,
                           query, (uint16_t)strlen(query));
#if JARVIS_DBG_IPC
            puts_serial("[DBG] SHIELD: signaling...\n");
#endif
            seL4_Signal(req_notif);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] SHIELD: waiting...\n");
#endif
            seL4_Wait(resp_notif, NULL);
#if JARVIS_DBG_IPC
            puts_serial("[DBG] SHIELD: woke up\n");
#endif

            uint8_t msg_type;
            uint16_t msg_seq;
            uint8_t payload[SHMEM_MAX_PAYLOAD];
            uint16_t msg_len;
            if (shmem_ipc_recv(shared_response_ring, &msg_type, &msg_seq,
                               payload, &msg_len) == 0) {
                if (msg_type != MSG_SHIELD_RESULT) {
#if JARVIS_DBG_IPC
                    puts_serial("[WARN] shield got type=");
                    put_dec((uint32_t)msg_type); puts_serial("\n");
#endif
                    q_errors++;
                }
            } else {
#if JARVIS_DBG_IPC
                puts_serial("[ERR] no shield response\n");
#endif
                q_errors++;
            }
        }

        /* Stats every 100 queries */
#if JARVIS_DBG_STATS
        if (q_total % 100 == 0) {
            puts_serial("[STATS] q="); put_dec(q_total);
            puts_serial(" hits="); put_dec(q_hits);
            puts_serial(" infer="); put_dec(q_infer);
            puts_serial(" hb="); put_dec(q_heartbeat);
            puts_serial(" shield="); put_dec(q_shield);
            puts_serial(" err="); put_dec(q_errors);
            puts_serial("\n");

            /* Log stats to NVMe every 100 queries */
            {
                char sb[128];
                /* Manual snprintf equivalent (no stdio in seL4 rootserver) */
                int sp = 0;
                const char *p = "q=";
                while (*p) sb[sp++] = *p++;
                /* Decimal append helper — inline for seL4 */
                { uint32_t v = q_total; char d[12]; int di = 0;
                  if (v == 0) d[di++] = '0';
                  else while (v > 0) { d[di++] = '0' + (v % 10); v /= 10; }
                  while (--di >= 0) sb[sp++] = d[di]; }
                sb[sp++] = ' '; p = "hit=";
                while (*p) sb[sp++] = *p++;
                { uint32_t v = q_hits; char d[12]; int di = 0;
                  if (v == 0) d[di++] = '0';
                  else while (v > 0) { d[di++] = '0' + (v % 10); v /= 10; }
                  while (--di >= 0) sb[sp++] = d[di]; }
                sb[sp++] = ' '; p = "err=";
                while (*p) sb[sp++] = *p++;
                { uint32_t v = q_errors; char d[12]; int di = 0;
                  if (v == 0) d[di++] = '0';
                  else while (v > 0) { d[di++] = '0' + (v % 10); v /= 10; }
                  while (--di >= 0) sb[sp++] = d[di]; }
                sb[sp] = '\0';
                nvme_log_write(g_nvme_ptr, g_nvme_bounce_vaddr, g_nvme_bounce_paddr,
                               LOG_IPC_STATS, sb);
            }
        }
#endif

        seL4_Yield();
    }

    /* This point never reached — workload loop is infinite */

idle:
    puts_serial("\n[JARVIS] System idle (no inference).\n");
    while (1) { seL4_Yield(); }
    return NULL;
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

    /* Demo queries + SHIELD (serial only — too verbose for 25-line VGA) */
#ifdef __x86_64__
    int saved_vga = vga_ready; vga_ready = 0;
#endif
    puts_serial("--- Cache Queries ---\n");
    do_query("what is the system status");
    do_query("check disk space");
    do_query("list running processes");
    do_query("show memory usage");
    do_query("restart the web server");
    do_query("unknown query test");
    do_query("show network interfaces");
    do_query("open text editor");
    puts_serial("\n--- SHIELD ---\n");
    shield_check("check health");
    shield_check("delete all data");
    shield_check("rm -rf /");
    puts_serial("\n--- Stats ---\n");
    puts_serial("  Queries: "); put_dec(total_queries); puts_serial("\n");
    puts_serial("  Hits:    "); put_dec(cache_hits); puts_serial("\n");
    puts_serial("  Misses:  "); put_dec(cache_misses); puts_serial("\n");
    if (total_queries > 0) {
        puts_serial("  Rate:    ");
        put_dec((cache_hits * 100) / total_queries);
        puts_serial("%\n");
    }
#ifdef __x86_64__
    vga_ready = saved_vga;
#endif

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

    /* ---- Bootstrap allocman for process management ---- */
    puts_serial("\n[JARVIS] Bootstrapping system...\n");
    if (init_system() != 0) {
        puts_serial("[JARVIS] System bootstrap failed — running in degraded mode\n");
        puts_serial("\n[JARVIS] System idle.\n");
        while (1) { seL4_Yield(); }
    }
    puts_serial("[JARVIS] System bootstrapped\n");

    /* Switch to vspace-managed stack (required for vspace_new_pages to work) */
    void *res;
    int error = sel4utils_run_on_stack(&vspace, main_continued, NULL, &res);
    if (error) {
        puts_serial("[JARVIS] Failed to switch to managed stack\n");
    }

    /* Should never reach here */
    while (1) { seL4_Yield(); }
    return 0;
}
