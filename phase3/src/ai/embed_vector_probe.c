/*
 * embed_vector_probe.c — Phase C / C/M1a Stage 2: VECTOR-PARITY harness (host, model-gated).
 *
 * Loads the Qwen3-Embedding-0.6B GGUF via the DEPLOYED quantized engine (qmodel_load), tokenizes
 * the 15 fixed probes via the Stage-1-proven path (gguf_vocab + tokenizer_encode), appends EOS
 * 151643 (add_eos=true, last-pooled), and runs qmodel_embed_last — the gated last-token embed
 * forward that pools the post-output_norm hidden state (llama_quant.c ~:1801, PRE-LM-head) and
 * L2-normalizes → a 1024-d vector per probe. Writes the 15 x dim raw little-endian float32 vectors
 * to argv[2]; cm1a_vector_parity.py diffs them against the two goldens (gates 2 + 3).
 *
 * MEASURE-FIRST: prints the loaded arch + rope_neox/embed_scale/use_gelu and ASSERTs rope_neox==1
 * for the qwen arch (the NEOX override, the #1 silent-parity breaker if wrong).
 *
 * Build (host, Windows clang / Linux gcc), single-threaded for determinism — SAME link set as
 * bench_engine.c (minus bench_engine.c, plus this file):
 *   clang -O2 -std=c11 -DJARVIS_EMBED=1 -D_CRT_SECURE_NO_WARNINGS -Wno-deprecated-declarations \
 *         -mavx2 -mfma -I phase3/src/ai \
 *         phase3/src/ai/embed_vector_probe.c phase3/src/ai/llama_quant.c phase3/src/ai/llama_load.c \
 *         phase3/src/ai/llama_forward.c phase3/src/ai/gguf_parser.c phase3/src/ai/gguf_vocab.c \
 *         phase3/src/ai/tokenizer.c phase3/src/ai/tensor_ops.c phase3/src/ai/dequant.c \
 *         phase3/src/ai/sampling.c phase3/src/ai/inference.c phase3/src/ai/ssm.c -o /tmp/evp
 * Run:  /tmp/evp <path-to-Qwen3-Embedding-0.6B-Q8_0.gguf> <out.bin>
 *
 * Gated JARVIS_EMBED (default 0): at EMBED=0 this file compiles to an inert stub, and the qwen/embed
 * changes in llama_load.c + llama_quant.c compile out (object-identical deployed engine).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JARVIS_EMBED
#define JARVIS_EMBED 0
#endif

#if JARVIS_EMBED

#include "llama_model.h"
#include "llama_quant.h"
#include "gguf_parser.h"
#include "gguf_vocab.h"
#include "tokenizer.h"

#if defined(_WIN32)
/* Windows host shim: the MSVC CRT lacks fmemopen (declared for the other TUs in
 * embed_host_fmemopen.h, force-included). gguf_open_memory() needs a readable FILE* over the
 * buffer; gguf_parser only fread's the (small) header/metadata region — the tensor weights are
 * read via the in-memory gguf_base pointer, not this FILE. Back it with a DELETE_ON_CLOSE temp
 * file so it self-cleans on gguf_close(). <windows.h> is confined to this leaf TU (its min/max/
 * ERROR macros never reach the engine sources). Host-parity harness ONLY. */
#include <stdint.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
FILE *fmemopen(void *buf, size_t size, const char *mode)
{
    (void)mode;
    char dir[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathA((DWORD)sizeof(dir), dir)) return NULL;
    if (!GetTempFileNameA(dir, "evp", 0, path)) return NULL;
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    int fd = _open_osfhandle((intptr_t)h, _O_RDWR | _O_BINARY);
    if (fd < 0) { CloseHandle(h); return NULL; }
    FILE *f = _fdopen(fd, "wb+");
    if (!f) { _close(fd); return NULL; }
    if (size && fwrite(buf, 1, size, f) != size) { fclose(f); return NULL; }
    rewind(f);
    return f;
}
#endif  /* _WIN32 */

#define EMBED_EOS_ID 151643        /* <|endoftext|>: add_eos_token=true, appended + last-pooled */

/* The 15 probes, in the golden order (cm1a_golden.py PROBE_TEXTS / golden_meta token_ids keys).
 * Byte-identical to embed_tokenize_probe.c's set — Stage-1 proved these tokenize 15/15 exact. */
static const char *const PROBES[] = {
    "what is a page fault",
    "how does dns work",
    "what is a mutex",
    "how do you find a value in a sorted array in logarithmic time",
    "what lightweight text format stores structured data as key-value pairs",
    "how do you protect a critical section so only one thread runs it",
    "what's the capital of France",
    "how do you bake sourdough bread",
    "how long have you been up",
    "what model are you running",
    "why doesn't adding more cpu cores speed up a single-threaded program",
    "what is a hash table",
    "explain how tcp handshake works",
    "what is public-key encryption",
    "what does DMA do",
};
#define N_PROBES ((int)(sizeof(PROBES) / sizeof(PROBES[0])))

/* 64-bit file offsets: an F32 GGUF of this model is ~2.38 GB, and `long` is 32 BITS on Windows, so
 * plain ftell() overflows past 2 GB and returns -1 (read fails before a single token is embedded).
 * LP64 (Linux/macOS) already has a 64-bit long, so plain fseek/ftell are correct there. */
#if defined(_WIN32)
typedef __int64 jep_off_t;
#define JEP_FSEEK _fseeki64
#define JEP_FTELL _ftelli64
#else
typedef long jep_off_t;
#define JEP_FSEEK fseek
#define JEP_FTELL ftell
#endif

static void *slurp(const char *path, jep_off_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (JEP_FSEEK(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    jep_off_t len = JEP_FTELL(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (JEP_FSEEK(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(buf); return NULL; }
    *out_len = len;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <Qwen3-Embedding-0.6B-Q8_0.gguf> <out.bin>\n"
                        "  writes %d x dim raw little-endian float32 embeddings to <out.bin>\n",
                        argv[0], N_PROBES);
        return 2;
    }

    jep_off_t model_len = 0;
    void *model_buf = slurp(argv[1], &model_len);
    if (!model_buf) { fprintf(stderr, "FATAL: cannot read %s\n", argv[1]); return 2; }

    gguf_ctx_t ctx;
    if (gguf_open_memory(&ctx, model_buf, (size_t)model_len) != 0) {
        fprintf(stderr, "FATAL: gguf_open_memory failed\n"); free(model_buf); return 2;
    }

    qmodel_t qm;
    if (qmodel_load(&qm, &ctx, model_buf) != 0) {
        fprintf(stderr, "FATAL: qmodel_load failed\n"); return 2;
    }

    const char *arch = gguf_get_kv_string(&ctx, "general.architecture");
    const int dim = qm.config.dim;
    fprintf(stderr, "[embed] arch=%s dim=%d n_layers=%d n_heads=%d n_kv_heads=%d head_dim=%d\n",
            arch ? arch : "(null)", dim, qm.config.n_layers, qm.config.n_heads,
            qm.config.n_kv_heads, qm.config.head_dim);
    fprintf(stderr, "[embed] rope_neox=%d embed_scale=%d use_gelu=%d rope_dim_count=%d rope_theta=%.1f\n",
            qm.config.rope_neox, qm.config.embed_scale, qm.config.use_gelu,
            qm.config.rope_dim_count, qm.config.rope_theta);

    /* MEASURE-FIRST assertion: qwen must be NEOX (the llama_load.c #if JARVIS_EMBED override). */
    if (arch && strncmp(arch, "qwen", 4) == 0 && !qm.config.rope_neox) {
        fprintf(stderr, "FATAL: qwen arch but rope_neox=0 — the NEOX override did NOT fire "
                        "(interleaved RoPE = wrong vectors). Check llama_load.c.\n");
        qmodel_free(&qm); gguf_close(&ctx); free(model_buf); return 3;
    }

    gguf_vocab_t vocab;
    if (gguf_vocab_extract(model_buf, (size_t)model_len, &vocab) != 0) {
        fprintf(stderr, "FATAL: gguf_vocab_extract failed\n"); return 2;
    }
    tokenizer_t tok;
    if (gguf_vocab_init_tokenizer(&vocab, &tok) != 0) {
        fprintf(stderr, "FATAL: gguf_vocab_init_tokenizer failed\n"); return 2;
    }

    llama_state_t state;
    if (llama_alloc_state(&state, &qm.config) != 0) {
        fprintf(stderr, "FATAL: llama_alloc_state failed\n"); return 2;
    }

    /* KV cache size for the per-probe reset (mirrors bench_engine.c). qwen3 is dense
     * (no SWA/head_dim_swa, no DeltaNet) so head_dim is the KV head dim and there is no SSM state. */
    size_t kv_bytes = (size_t)state.kv_n_layers * (size_t)state.max_seq_len *
                      (size_t)qm.config.n_kv_heads * (size_t)qm.config.head_dim * sizeof(float);

    FILE *out = fopen(argv[2], "wb");
    if (!out) { fprintf(stderr, "FATAL: cannot open %s for write\n", argv[2]); return 2; }

    float *vec = (float *)malloc((size_t)dim * sizeof(float));
    if (!vec) { fprintf(stderr, "FATAL: vec alloc\n"); return 2; }

    int ids[512];
    for (int p = 0; p < N_PROBES; p++) {
        int n = tokenizer_encode(&tok, PROBES[p], ids, 511);
        if (n < 0) { fprintf(stderr, "FATAL: encode failed for probe %d\n", p); return 2; }
        ids[n++] = EMBED_EOS_ID;           /* add_eos=true — the last-pooled position */

        /* Reset the autoregressive state so each probe is an independent sequence. */
        state.pos = 0;
        memset(state.key_cache, 0, kv_bytes);
        memset(state.value_cache, 0, kv_bytes);

        qmodel_embed_last(&qm, &state, ids, n, vec);

        if (fwrite(vec, sizeof(float), (size_t)dim, out) != (size_t)dim) {
            fprintf(stderr, "FATAL: write failed for probe %d\n", p); return 2;
        }
        /* |v|^2 should read ~1.0000 after L2 (sum of squares of a unit vector); measure-first eyeball. */
        double nn = 0.0; for (int i = 0; i < dim; i++) nn += (double)vec[i] * vec[i];
        fprintf(stderr, "  [%2d] toks=%2d |v|^2=%.4f  v[0..2]=% .5f % .5f % .5f  %s\n",
                p, n, nn, vec[0], vec[1], vec[2], PROBES[p]);
    }

    fclose(out);
    free(vec);
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    qmodel_free(&qm);
    gguf_close(&ctx);
    free(model_buf);
    fprintf(stderr, "[embed] wrote %d x %d float32 -> %s\n", N_PROBES, dim, argv[2]);
    return 0;
}

#else  /* !JARVIS_EMBED — inert stub so a default build compiles */

int main(void)
{
    fprintf(stderr, "embed_vector_probe: built with JARVIS_EMBED=0 (inert). "
                    "Rebuild with -DJARVIS_EMBED=1 for the C/M1a vector-parity harness.\n");
    return 0;
}

#endif /* JARVIS_EMBED */
