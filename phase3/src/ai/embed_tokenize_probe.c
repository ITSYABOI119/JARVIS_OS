/*
 * embed_tokenize_probe.c — Phase C / C/M1a Stage 1: TOKEN-PARITY harness (host, model-gated).
 *
 * Loads the Qwen3-Embedding-0.6B GGUF vocab via the deployed gguf_vocab + tokenizer path, encodes the
 * 15 fixed probe strings (the keys of phase3/scripts/embed/golden_meta.json["token_ids"], hardcoded
 * here in the SAME order), appends the EOS 151643 (<|endoftext|> — the token Qwen3-Embedding pools),
 * and prints one line per probe:  <probe>\t<comma-separated-token-ids>
 *
 * cm1a_token_parity.py diffs this stdout against golden_meta.json EXACTLY (no tolerance). This is the
 * Stage-1 GATE. TOKENIZATION ONLY — no forward pass / RoPE / vectors (that is Stage 2).
 *
 * Build (host):  gcc -O2 -DJARVIS_EMBED=1 -mavx2 -mfma -I phase3/src/ai \
 *                    phase3/src/ai/embed_tokenize_probe.c phase3/src/ai/tokenizer.c \
 *                    phase3/src/ai/gguf_vocab.c -o /tmp/embed_tok_probe
 * Run:           /tmp/embed_tok_probe <path-to-Qwen3-Embedding-0.6B-Q8_0.gguf>
 *
 * Gated JARVIS_EMBED (default 0): at EMBED=0 this file compiles to an inert stub, and the qwen/embed
 * changes in tokenizer.c compile out, so the DEPLOYED SentencePiece/Gemma tokenizer is byte-identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JARVIS_EMBED
#define JARVIS_EMBED 0
#endif

#if JARVIS_EMBED

#include "tokenizer.h"
#include "gguf_vocab.h"

#define EMBED_EOS_ID 151643        /* <|endoftext|>: add_eos_token=true, appended + last-pooled */

/* The 15 probes, in the golden order (cm0_bench.py PROBE_TEXTS / golden_meta token_ids keys). */
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

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(buf); return NULL; }
    *out_len = (size_t)len;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <Qwen3-Embedding-0.6B-Q8_0.gguf> [strings-file]\n"
                        "  no strings-file -> the 15 golden probes; with one -> its newline-separated\n"
                        "  lines (the STRESS set, for C/M2-robustness measurement).\n", argv[0]);
        return 2;
    }
    size_t model_len = 0;
    char *model_buf = slurp(argv[1], &model_len);
    if (!model_buf) { fprintf(stderr, "FATAL: cannot read %s\n", argv[1]); return 2; }

    /* Optional strings-file (argv[2]): tokenize its lines instead of the 15 golden probes. Lets the
     * stress harness feed arbitrary inputs (numbers/punctuation/contractions/...) to find any real
     * divergence beyond the gate set. */
    const char **probes = PROBES;
    int n_probes = N_PROBES;
    char *sfile = NULL;
    char **slines = NULL;
    if (argc >= 3) {
        size_t slen = 0;
        sfile = slurp(argv[2], &slen);
        if (!sfile) { fprintf(stderr, "FATAL: cannot read strings-file %s\n", argv[2]); free(model_buf); return 2; }
        /* count + split on '\n' (ignore blank lines and a trailing newline) */
        int cap = 64, cnt = 0;
        slines = (char **)malloc(sizeof(char *) * (size_t)cap);
        char *p = sfile, *end = sfile + slen;
        while (p < end) {
            char *nl = p;
            while (nl < end && *nl != '\n') nl++;
            if (nl > p) {
                if (cnt == cap) { cap *= 2; slines = (char **)realloc(slines, sizeof(char *) * (size_t)cap); }
                *nl = '\0';
                if (nl > p && nl[-1] == '\r') nl[-1] = '\0';   /* strip CR */
                if (slines[cnt] = p, p[0]) cnt++;
            }
            p = nl + 1;
        }
        probes = (const char **)slines;
        n_probes = cnt;
    }

    gguf_vocab_t vocab;
    if (gguf_vocab_extract(model_buf, model_len, &vocab) != 0) {
        fprintf(stderr, "FATAL: gguf_vocab_extract failed\n"); free(model_buf); return 2;
    }
    fprintf(stderr, "vocab_size=%d has_merges=%d add_space_prefix=%d bos=%d eos=%d\n",
            vocab.vocab_size, vocab.has_merges, vocab.add_space_prefix, vocab.bos_id, vocab.eos_id);

    tokenizer_t tok;
    if (gguf_vocab_init_tokenizer(&vocab, &tok) != 0) {
        fprintf(stderr, "FATAL: gguf_vocab_init_tokenizer failed\n");
        gguf_vocab_free(&vocab); free(model_buf); return 2;
    }

    int ids[512];
    for (int p = 0; p < n_probes; p++) {
        int n = tokenizer_encode(&tok, probes[p], ids, 511);
        if (n < 0) { fprintf(stderr, "FATAL: encode failed for probe %d\n", p); return 2; }
        ids[n++] = EMBED_EOS_ID;           /* append EOS (the box embed path adds it; Stage-1 harness does it here) */
        printf("%s\t", probes[p]);
        for (int i = 0; i < n; i++) printf(i ? ",%d" : "%d", ids[i]);
        printf("\n");
    }

    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    free(slines);
    free(sfile);
    free(model_buf);
    return 0;
}

#else  /* !JARVIS_EMBED — inert stub so a default build compiles */

int main(void)
{
    fprintf(stderr, "embed_tokenize_probe: built with JARVIS_EMBED=0 (inert). "
                    "Rebuild with -DJARVIS_EMBED=1 for the C/M1a token-parity harness.\n");
    return 0;
}

#endif /* JARVIS_EMBED */
