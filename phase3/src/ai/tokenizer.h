#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

#define TOKENIZER_MAX_TOKEN_LEN 128

typedef struct {
    char   **tokens;      /* Array of token strings (malloc'd copies) */
    float   *scores;      /* Merge priority per token */
    int      vocab_size;
    int      bos_id;
    int      eos_id;
    int     *ht_table;    /* Hash table: token index or -1 (empty) */
    int      ht_capacity; /* Power of 2, >= 2 * vocab_size */
    int      add_space_prefix; /* 1 = prepend ▁ to input (default), 0 = don't (Gemma 4) */
    /* E (Phase 6 K/M2b-2): pre-allocated per-encode scratch so tokenizer_encode never
     * malloc/free per query (closes the mid-tokenize arena-tear window). Allocated once at
     * tokenizer_init; NULL/0 => encode falls back to per-query malloc (still correct). */
    char    *enc_sp_scratch;    /* space-marker substitution buffer, >= cap*3 + 4 bytes */
    void    *enc_syms_scratch;  /* bpe_sym_t[cap*3 + 4] (void* — bpe_sym_t is file-local) */
    int      enc_scratch_cap;   /* max ORIGINAL text_len served without falling back to malloc */
} tokenizer_t;

int  tokenizer_init(tokenizer_t *t, const char **tokens, const float *scores,
                    int vocab_size, int bos_id, int eos_id);
int  tokenizer_encode(const tokenizer_t *t, const char *text,
                      int *out_ids, int max_tokens);
int  tokenizer_decode(const tokenizer_t *t, const int *token_ids, int n_tokens,
                      char *out_text, int max_len);
int  tokenizer_find(const tokenizer_t *t, const char *token_str);
void tokenizer_free(tokenizer_t *t);

#endif
