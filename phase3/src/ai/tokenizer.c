#define _POSIX_C_SOURCE 200809L
#include "tokenizer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

static uint64_t fnv1a_hash(const char *str)
{
    uint64_t h = 14695981039346656037ULL;
    while (*str) {
        h ^= (uint8_t)*str++;
        h *= 1099511628211ULL;
    }
    return h;
}

static int next_pow2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

int tokenizer_init(tokenizer_t *t, const char **tokens, const float *scores,
                   int vocab_size, int bos_id, int eos_id)
{
    t->vocab_size = vocab_size;
    t->bos_id = bos_id;
    t->eos_id = eos_id;
    t->tokens = NULL;
    t->scores = NULL;
    t->ht_table = NULL;
    t->ht_capacity = 0;
    t->add_space_prefix = 1;  /* default: prepend ▁ (overridden by GGUF flag) */

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

    /* Build hash table for O(1) tokenizer_find() */
    t->ht_capacity = next_pow2(vocab_size * 2);
    t->ht_table = (int *)malloc(sizeof(int) * (size_t)t->ht_capacity);
    if (!t->ht_table) { tokenizer_free(t); return -1; }
    memset(t->ht_table, 0xFF, sizeof(int) * (size_t)t->ht_capacity); /* fill with -1 */

    for (int i = 0; i < vocab_size; i++) {
        uint64_t h = fnv1a_hash(t->tokens[i]) % (uint64_t)t->ht_capacity;
        while (t->ht_table[h] != -1)
            h = (h + 1) % (uint64_t)t->ht_capacity;
        t->ht_table[h] = i;
    }

    return 0;
}

int tokenizer_find(const tokenizer_t *t, const char *token_str)
{
    if (!t->ht_table) {
        /* Fallback: linear scan if hash table not built */
        for (int i = 0; i < t->vocab_size; i++)
            if (strcmp(t->tokens[i], token_str) == 0) return i;
        return -1;
    }
    uint64_t h = fnv1a_hash(token_str) % (uint64_t)t->ht_capacity;
    /* SEC-036: Bound probe to ht_capacity iterations to guarantee termination
     * even if the table is full (shouldn't happen with 2x sizing, but defense-in-depth). */
    int probes = t->ht_capacity;
    while (t->ht_table[h] != -1 && probes-- > 0) {
        if (strcmp(t->tokens[t->ht_table[h]], token_str) == 0)
            return t->ht_table[h];
        h = (h + 1) % (uint64_t)t->ht_capacity;
    }
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

    /* Space-marker pre-processing: substitute literal spaces with the vocab's
     * word-boundary marker so that BPE merges can build " word" tokens.
     *
     *   SentencePiece (LLaMA 1/2, Gemma, Mistral): marker = ▁  (U+2581, 3 bytes)
     *   GPT-2 BPE    (Llama 3, Qwen, Falcon):      marker = Ġ  (U+0120, 2 bytes)
     *
     * Leading-marker prepend:
     *   SentencePiece: honor add_space_prefix (default 1, Gemma 4 sets 0)
     *   GPT-2 BPE:     never prepend (the first word is "The", not " The")
     *
     * If neither marker is in the vocab, skip substitution entirely — the
     * caller's text is tokenized character-by-character as-is. */
    char *sp_text = NULL;
    const char *sp_marker = NULL;
    int sp_len = 0;
    int sp_prepend = 0;
    if (tokenizer_find(t, "\xe2\x96\x81") >= 0) {
        /* SentencePiece ▁ — honor add_space_prefix for leading marker */
        sp_marker = "\xe2\x96\x81";
        sp_len = 3;
        sp_prepend = t->add_space_prefix;
    } else if (tokenizer_find(t, "\xc4\xa0") >= 0) {
        /* GPT-2 BPE Ġ — never prepend leading marker */
        sp_marker = "\xc4\xa0";
        sp_len = 2;
        sp_prepend = 0;
    }
    if (sp_marker) {
        sp_text = (char *)malloc((size_t)text_len * 3 + 4);
        if (!sp_text) return -1;
        int wp = 0;
        if (sp_prepend) {
            memcpy(sp_text + wp, sp_marker, (size_t)sp_len);
            wp += sp_len;
        }
        for (int i = 0; i < text_len; i++) {
            if (text[i] == ' ') {
                memcpy(sp_text + wp, sp_marker, (size_t)sp_len);
                wp += sp_len;
            } else {
                sp_text[wp++] = text[i];
            }
        }
        sp_text[wp] = '\0';
        text = sp_text;
        text_len = wp;
    }

    /* Initialize: one symbol per UTF-8 character (not per byte).
     * For ASCII: 1 byte. For ▁ (0xE2 0x96 0x81): 3 bytes. */
    int max_syms = text_len;
    bpe_sym_t *syms = (bpe_sym_t *)malloc(sizeof(bpe_sym_t) * (size_t)max_syms);
    if (!syms) { free(sp_text); return -1; }

    int n_syms = 0;
    {
        int pos = 0;
        while (pos < text_len && n_syms < max_syms) {
            unsigned char c = (unsigned char)text[pos];
            int char_len = 1;
            if (c >= 0xF0) char_len = 4;
            else if (c >= 0xE0) char_len = 3;
            else if (c >= 0xC0) char_len = 2;
            if (pos + char_len > text_len) char_len = 1;

            syms[n_syms].prev = n_syms - 1;
            syms[n_syms].next = -1;
            syms[n_syms].start = pos;
            syms[n_syms].len = char_len;

            char ch[8];
            memcpy(ch, text + pos, (size_t)char_len);
            ch[char_len] = '\0';
            syms[n_syms].token_id = tokenizer_find(t, ch);

            if (n_syms > 0) syms[n_syms - 1].next = n_syms;
            n_syms++;
            pos += char_len;
        }
    }

    /* BPE merge loop: repeatedly merge the highest-scoring adjacent pair */
    char merge_buf[TOKENIZER_MAX_TOKEN_LEN];
    while (1) {
        int best_i = -1;
        float best_score = -FLT_MAX;
        int best_id = -1;

        /* Find best adjacent pair to merge */
        for (int i = 0; i < n_syms; i++) {
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
    for (int i = 0; i < n_syms; i++) {
        if (syms[i].len > 0) { head = i; break; }
    }
    for (int i = head; i >= 0 && out_count < max_tokens; i = syms[i].next) {
        out_ids[out_count++] = syms[i].token_id;
    }

    free(syms);
    free(sp_text);
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

    /* SentencePiece reverse mapping: ▁ (U+2581, 0xE2 0x96 0x81) → space.
     * Applied before GPT-2 mapping (which is harmless for SentencePiece). */
    {
        int r2 = 0, w2 = 0;
        while (r2 < pos) {
            unsigned char c = (unsigned char)out_text[r2];
            if (c == 0xE2 && r2 + 2 < pos &&
                (unsigned char)out_text[r2+1] == 0x96 &&
                (unsigned char)out_text[r2+2] == 0x81) {
                out_text[w2++] = ' ';
                r2 += 3;
            } else {
                out_text[w2++] = out_text[r2++];
            }
        }
        out_text[w2] = '\0';
        pos = w2;
    }

    /* Strip leading space (SentencePiece prepends ▁ which becomes leading space) */
    if (pos > 0 && out_text[0] == ' ') {
        memmove(out_text, out_text + 1, (size_t)pos);
        pos--;
    }

    /* GPT-2 BPE reverse mapping: 2-byte UTF-8 -> original byte.
     * All 256 byte values are mapped to printable Unicode characters.
     * Bytes 0x21-0x7E, 0xA1-0xAC, 0xAE-0xFF map to themselves (identity).
     * The remaining 68 bytes (0x00-0x20, 0x7F-0xA0, 0xAD) map to
     * Unicode U+0100-U+0143 in order:
     *   U+0100-U+013F (UTF-8: 0xC4 0x80-0xBF): first 64 of 68
     *   U+0140-U+0143 (UTF-8: 0xC5 0x80-0x83): last 4 of 68
     * In-place replacement: 2-byte sequence -> 1 byte, shrinks string. */
    {
        static const int gpt2_c4_map[64] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, /* U+0100-0107 */
            0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F, /* U+0108-010F */
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17, /* U+0110-0117 */
            0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F, /* U+0118-011F */
            0x20,0x7F,0x80,0x81,0x82,0x83,0x84,0x85, /* U+0120-0127 */
            0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D, /* U+0128-012F */
            0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95, /* U+0130-0137 */
            0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D, /* U+0138-013F */
        };
        static const int gpt2_c5_map[4] = {
            0x9E,0x9F,0xA0,0xAD, /* U+0140-0143 */
        };

        int r = 0, w = 0;
        while (r < pos) {
            unsigned char c = (unsigned char)out_text[r];
            if (c == 0xC4 && r + 1 < pos) {
                unsigned char c2 = (unsigned char)out_text[r + 1];
                if (c2 >= 0x80 && c2 <= 0xBF) {
                    out_text[w++] = (char)gpt2_c4_map[c2 - 0x80];
                    r += 2;
                    continue;
                }
            } else if (c == 0xC5 && r + 1 < pos) {
                unsigned char c2 = (unsigned char)out_text[r + 1];
                if (c2 >= 0x80 && c2 <= 0x83) {
                    out_text[w++] = (char)gpt2_c5_map[c2 - 0x80];
                    r += 2;
                    continue;
                }
            }
            out_text[w++] = out_text[r++];
        }
        out_text[w] = '\0';
        pos = w;
    }

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
    free(t->ht_table);
    t->ht_table = NULL;
    t->ht_capacity = 0;
    t->vocab_size = 0;
}
