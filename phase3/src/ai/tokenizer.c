#define _POSIX_C_SOURCE 200809L
#include "tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

int tokenizer_init(tokenizer_t *t, const char **tokens, const float *scores,
                   int vocab_size, int bos_id, int eos_id)
{
    t->vocab_size = vocab_size;
    t->bos_id = bos_id;
    t->eos_id = eos_id;
    t->tokens = NULL;
    t->scores = NULL;

    t->tokens = (char **)malloc(sizeof(char *) * (size_t)vocab_size);
    t->scores = (float *)malloc(sizeof(float) * (size_t)vocab_size);
    if (!t->tokens || !t->scores) { tokenizer_free(t); return -1; }

    /* Zero out tokens array so tokenizer_free is safe on partial init */
    memset(t->tokens, 0, sizeof(char *) * (size_t)vocab_size);

    for (int i = 0; i < vocab_size; i++) {
        t->tokens[i] = strdup(tokens[i]);
        if (!t->tokens[i]) { tokenizer_free(t); return -1; }
        t->scores[i] = scores ? scores[i] : 0.0f;
    }
    return 0;
}

int tokenizer_find(const tokenizer_t *t, const char *token_str)
{
    /* TODO: hash table for O(1) lookup. Linear scan for now. */
    for (int i = 0; i < t->vocab_size; i++)
        if (strcmp(t->tokens[i], token_str) == 0) return i;
    return -1;
}

typedef struct {
    int prev, next;     /* Linked list indices (-1 = none) */
    int start, len;     /* Substring of input text */
    int token_id;
} bpe_sym_t;

int tokenizer_encode(const tokenizer_t *t, const char *text,
                     int *out_ids, int max_tokens)
{
    int text_len = (int)strlen(text);
    if (text_len == 0 || max_tokens <= 0) return 0;

    /* Initialize: one symbol per character */
    bpe_sym_t *syms = (bpe_sym_t *)malloc(sizeof(bpe_sym_t) * (size_t)text_len);
    if (!syms) return -1;

    for (int i = 0; i < text_len; i++) {
        syms[i].prev = i - 1;
        syms[i].next = (i + 1 < text_len) ? i + 1 : -1;
        syms[i].start = i;
        syms[i].len = 1;
        /* Find single-char token */
        char ch[2] = { text[i], '\0' };
        syms[i].token_id = tokenizer_find(t, ch);
    }

    /* BPE merge loop: repeatedly merge the highest-scoring adjacent pair */
    char merge_buf[TOKENIZER_MAX_TOKEN_LEN];
    while (1) {
        int best_i = -1;
        float best_score = -FLT_MAX;
        int best_id = -1;

        /* Find best adjacent pair to merge */
        for (int i = 0; i < text_len; i++) {
            if (syms[i].len == 0) continue;  /* Deleted */
            int j = syms[i].next;
            if (j < 0) continue;

            /* Build candidate merged string */
            int total_len = syms[i].len + syms[j].len;
            if (total_len >= TOKENIZER_MAX_TOKEN_LEN) continue;
            memcpy(merge_buf, text + syms[i].start, (size_t)syms[i].len);
            memcpy(merge_buf + syms[i].len, text + syms[j].start, (size_t)syms[j].len);
            merge_buf[total_len] = '\0';

            int id = tokenizer_find(t, merge_buf);
            if (id >= 0 && t->scores[id] > best_score) {
                best_score = t->scores[id];
                best_i = i;
                best_id = id;
            }
        }

        if (best_i < 0) break;  /* No more merges possible */

        /* Merge best pair: absorb syms[j] into syms[best_i] */
        int j = syms[best_i].next;
        syms[best_i].len += syms[j].len;
        syms[best_i].token_id = best_id;
        syms[best_i].next = syms[j].next;
        if (syms[j].next >= 0) syms[syms[j].next].prev = best_i;
        syms[j].len = 0;  /* Mark deleted */
    }

    /* Collect output: walk linked list from first active symbol */
    int out_count = 0;
    int head = -1;
    for (int i = 0; i < text_len; i++) {
        if (syms[i].len > 0) { head = i; break; }
    }
    for (int i = head; i >= 0 && out_count < max_tokens; i = syms[i].next) {
        out_ids[out_count++] = syms[i].token_id;
    }

    free(syms);
    return out_count;
}

int tokenizer_decode(const tokenizer_t *t, const int *token_ids, int n_tokens,
                     char *out_text, int max_len)
{
    if (max_len <= 0) return -1;
    out_text[0] = '\0';

    int pos = 0;
    for (int i = 0; i < n_tokens; i++) {
        int id = token_ids[i];
        if (id < 0 || id >= t->vocab_size) continue;  /* Skip invalid */

        int tok_len = (int)strlen(t->tokens[id]);
        if (pos + tok_len >= max_len) break;  /* Would overflow */

        memcpy(out_text + pos, t->tokens[id], (size_t)tok_len);
        pos += tok_len;
    }
    out_text[pos] = '\0';
    return pos;
}

void tokenizer_free(tokenizer_t *t)
{
    if (t->tokens) {
        for (int i = 0; i < t->vocab_size; i++) {
            free(t->tokens[i]);  /* free(NULL) is safe */
        }
        free(t->tokens);
        t->tokens = NULL;
    }
    if (t->scores) {
        free(t->scores);
        t->scores = NULL;
    }
    t->vocab_size = 0;
}
