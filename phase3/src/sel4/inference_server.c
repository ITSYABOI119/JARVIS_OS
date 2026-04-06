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

/* ---- Process incoming IPC requests ---- */

static void handle_query(shmem_ring_t *response_ring, seL4_CPtr resp_notif,
                          uint16_t seq, const char *query, uint16_t query_len,
                          qmodel_t *qm, llama_state_t *state, tokenizer_t *tok,
                          int bos_id)
{
    /* Tokenize: BOS + query */
    int prompt_ids[64];
    prompt_ids[0] = bos_id;

    /* Null-terminate the query */
    char query_buf[241];
    int qlen = query_len > 240 ? 240 : query_len;
    memcpy(query_buf, query, (size_t)qlen);
    query_buf[qlen] = '\0';

    puts_serial("[PB] handle_query: \"");
    puts_serial(query_buf);
    puts_serial("\"\n");

    int n_prompt = 1 + tokenizer_encode(tok, query_buf, prompt_ids + 1, 63);

    /* Reset state for new generation */
    state->pos = 0;
    size_t kv_bytes = (size_t)qm->config.n_layers * state->max_seq_len *
                      qm->config.n_kv_heads * qm->config.head_dim * sizeof(float);
    memset(state->key_cache, 0, kv_bytes);
    memset(state->value_cache, 0, kv_bytes);
    puts_serial("[PB] KV reset done\n");

    /* Generate */
    puts_serial("[PB] generating...\n");
    int output_ids[64];
    int n_gen = qmodel_generate(qm, state, prompt_ids, n_prompt,
                                 output_ids, 50, tok->eos_id,
                                 0.0f, 1, 42);
    puts_serial("[PB] generated "); put_dec((uint32_t)n_gen); puts_serial(" tokens\n");

    /* Decode to text */
    char text_out[512];
    int text_len = tokenizer_decode(tok, output_ids, n_gen, text_out, sizeof(text_out));
    if (text_len < 0) text_len = 0;
    puts_serial("[PB] decoded "); put_dec((uint32_t)text_len); puts_serial(" bytes\n");

    /* Send response — split into multiple messages if >240 bytes */
    int offset = 0;
    uint16_t msg_seq = seq;
    puts_serial("[PB] send loop start\n");
    /* Ring health check */
    puts_serial("[PB] ring @0x"); put_hex((uint32_t)(uintptr_t)response_ring);
    puts_serial(" magic=0x"); put_hex(response_ring->header.magic);
    puts_serial(" w="); put_dec(response_ring->header.write_idx);
    puts_serial(" r="); put_dec(response_ring->header.read_idx);
    puts_serial("\n");
    while (offset < text_len) {
        int chunk = text_len - offset;
        if (chunk > SHMEM_MAX_PAYLOAD) chunk = SHMEM_MAX_PAYLOAD;
        puts_serial("[PB] chunk @"); put_dec((uint32_t)offset);
        puts_serial(" len="); put_dec((uint32_t)chunk); puts_serial("\n");
        int rc = shmem_ipc_send(response_ring, MSG_RESPONSE, msg_seq,
                       text_out + offset, (uint16_t)chunk);
        puts_serial("[PB] send rc="); put_dec((uint32_t)rc); puts_serial("\n");
        offset += chunk;
        msg_seq++;
    }

    /* If empty response, send empty message */
    if (text_len == 0) {
        shmem_ipc_send(response_ring, MSG_RESPONSE, seq, "", 0);
    }

    puts_serial("[PB] signaling Process A\n");
    seL4_Signal(resp_notif);
    puts_serial("[PB] response sent\n");
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

    /* Signal Process A that we're ready — eliminates startup race */
    shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
    seL4_Signal(resp_notif);

    /* ---- Main IPC loop ---- */
    while (1) {
        /* Wait for signal from Process A */
        seL4_Wait(req_notif, NULL);

        /* Process all pending requests */
        uint8_t msg_type;
        uint16_t msg_seq;
        uint8_t payload[SHMEM_MAX_PAYLOAD];
        uint16_t msg_len;

        while (shmem_ipc_recv(request_ring, &msg_type, &msg_seq, payload, &msg_len) == 0) {
            switch (msg_type) {
            case MSG_QUERY:
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
    while (1) {
        seL4_Yield();
    }

    return 0;
}
