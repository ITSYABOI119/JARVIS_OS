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

#if JARVIS_M1_MEASURE
static inline uint64_t m1_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#endif

#include "gguf_parser.h"
#include "llama_model.h"
#include "llama_quant.h"
#include "gguf_vocab.h"
#include "tokenizer.h"
#include "sampling.h"

#ifdef JARVIS_HAS_MODEL
extern const unsigned char _binary_model_gguf_start[];
extern const unsigned char _binary_model_gguf_end[];
#endif

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
    /* Build Gemma 4 chat template prompt with direct control token IDs:
     *   <bos> <|turn> user \n {query} <turn|> \n <|turn> model \n <|think|>
     * Tokens: bos=2, <|turn>=105, user=2364, \n=107, <turn|>=106, model=4368, <|think|>=98
     * Stop on <eos>=1, NOT eos_id=106 (that's <turn|> which model emits first). */
    int prompt_ids[128];
    int n_prompt = 0;
    prompt_ids[n_prompt++] = bos_id;        /* <bos> */
    prompt_ids[n_prompt++] = 105;           /* <|turn> */
    prompt_ids[n_prompt++] = 2364;          /* user */
    prompt_ids[n_prompt++] = 107;           /* \n */

    /* Null-terminate the query */
    char query_buf[241];
    int qlen = query_len > 240 ? 240 : query_len;
    memcpy(query_buf, query, (size_t)qlen);
    query_buf[qlen] = '\0';

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

    /* Encode user text */
    n_prompt += tokenizer_encode(tok, query_buf, prompt_ids + n_prompt,
                                  128 - n_prompt - 6);

    /* Close user turn + open model turn + think */
    prompt_ids[n_prompt++] = 106;           /* <turn|> */
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 105;           /* <|turn> */
    prompt_ids[n_prompt++] = 4368;          /* model */
    prompt_ids[n_prompt++] = 107;           /* \n */
    prompt_ids[n_prompt++] = 98;            /* <|think|> */

    /* DEBUG: print token IDs for comparison against llama.cpp reference */
    printf("[PB] tokens (%d):", n_prompt);
    for (int i = 0; i < n_prompt && i < 20; i++)
        printf(" %d", prompt_ids[i]);
    printf("\n");

    /* Reset state for new generation */
    state->pos = 0;
    size_t kv_bytes = (size_t)qm->config.n_layers * state->max_seq_len *
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
#if JARVIS_M1_MEASURE
    uint64_t m1_t0 = m1_rdtsc();
#endif
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
#if JARVIS_M1_MEASURE
    uint64_t m1_cyc = m1_rdtsc() - m1_t0;
#endif

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

/* ---- Main ---- */

int main(int argc, char **argv)
{
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

    if (request_ring->header.magic != SHMEM_MAGIC ||
        response_ring->header.magic != SHMEM_MAGIC) {
        puts_serial("[Process B] ERROR: shared memory not initialized by Process A\n");
        goto idle;
    }
    puts_serial("[Process B] IPC rings validated\n");
    g_resp_ring = response_ring;  /* Enable pb_log IPC transport */

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

    qmodel_t qm;
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
    gguf_vocab_t vocab;
    err = gguf_vocab_extract(model_data, model_data_size, &vocab);
    if (err) {
        puts_serial("[Process B] Vocab extraction failed\n");
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        goto idle;
    }

    tokenizer_t tok;
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
    llama_state_t state;
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
        size_t kv_bytes = (size_t)qm.config.n_layers * state.max_seq_len *
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

    /* Signal Process A that we're ready — eliminates startup race */
    shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
    seL4_Signal(resp_notif);

#if JARVIS_AVX2_PROBE
    /* M0: confirm the kernel enabled AVX state-saving (XCR0.AVX) at PB startup. */
    avx2_probe_init("PB");
#endif

    /* ---- Main IPC loop ---- */
    uint32_t pb_query_count = 0;
    while (1) {
        pb_query_count++;
        pb_log_num("[PB] Waiting for query #", pb_query_count, "");

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
                             &qm, &state, &tok, vocab.bos_id);
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

    return 0;
}
