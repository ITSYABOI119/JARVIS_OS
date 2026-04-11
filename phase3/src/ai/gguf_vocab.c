/*
 * JARVIS AI-OS Phase 3 -- GGUF Vocabulary Extractor
 *
 * Walks raw GGUF binary data to extract tokenizer vocabulary arrays.
 * The standard gguf_parser.c skips array data; this module reads
 * the specific arrays needed for the BPE tokenizer.
 *
 * GGUF binary layout reminder (little-endian):
 *   Header: magic(u32) version(u32) n_tensors(u64) n_kv(u64)
 *   KV pairs: key(string) type(u32) value(varies)
 *   String: len(u64) + chars[len]
 *   Array:  elem_type(u32) count(u64) elements[count]
 */

#include "gguf_vocab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ---- FNV-1a hash (same as tokenizer.c) ---- */

static uint64_t fnv1a_hash(const char *str)
{
    uint64_t h = 14695981039346656037ULL;
    while (*str) { h ^= (uint8_t)*str++; h *= 1099511628211ULL; }
    return h;
}

/* ---- Safe binary reader ---- */

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} buf_reader_t;

static int buf_read(buf_reader_t *r, void *dst, size_t n)
{
    if (r->pos + n > r->len) return -1;
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
    return 0;
}

static int buf_skip(buf_reader_t *r, size_t n)
{
    if (r->pos + n > r->len) return -1;
    r->pos += n;
    return 0;
}

static int buf_read_u32(buf_reader_t *r, uint32_t *out)
{
    return buf_read(r, out, 4);
}

static int buf_read_u64(buf_reader_t *r, uint64_t *out)
{
    return buf_read(r, out, 8);
}

static int buf_read_i32(buf_reader_t *r, int32_t *out)
{
    return buf_read(r, out, 4);
}

/* Read a GGUF string: u64 len + chars. Returns malloc'd null-terminated copy. */
static char *buf_read_gguf_string_alloc(buf_reader_t *r)
{
    uint64_t len;
    if (buf_read_u64(r, &len)) return NULL;
    if (len > 1024 * 1024) return NULL; /* sanity: 1 MB max per string */
    if (r->pos + len > r->len) return NULL;

    char *s = (char *)malloc((size_t)len + 1);
    if (!s) return NULL;
    memcpy(s, r->data + r->pos, (size_t)len);
    s[len] = '\0';
    r->pos += (size_t)len;
    return s;
}

/* Read a GGUF string into a fixed buffer, null-terminate. Returns 0 or -1. */
static int buf_read_gguf_string_fixed(buf_reader_t *r, char *out, size_t out_size)
{
    uint64_t len;
    if (buf_read_u64(r, &len)) return -1;
    if (r->pos + len > r->len) return -1;

    size_t copy = (len < out_size - 1) ? (size_t)len : (out_size - 1);
    memcpy(out, r->data + r->pos, copy);
    out[copy] = '\0';
    r->pos += (size_t)len;
    return 0;
}

/* Size of a scalar GGUF value type */
static size_t scalar_size(uint32_t type)
{
    switch (type) {
        case GGUF_TYPE_UINT8:   case GGUF_TYPE_INT8:  case GGUF_TYPE_BOOL: return 1;
        case GGUF_TYPE_UINT16:  case GGUF_TYPE_INT16:  return 2;
        case GGUF_TYPE_UINT32:  case GGUF_TYPE_INT32:  case GGUF_TYPE_FLOAT32: return 4;
        case GGUF_TYPE_UINT64:  case GGUF_TYPE_INT64:  case GGUF_TYPE_FLOAT64: return 8;
        default: return 0;
    }
}

/* Skip a single GGUF value. depth bounds recursion for arrays. */
static int skip_value(buf_reader_t *r, uint32_t type, int depth)
{
    if (depth > 8) return -1;

    if (type == GGUF_TYPE_STRING) {
        uint64_t len;
        if (buf_read_u64(r, &len)) return -1;
        return buf_skip(r, (size_t)len);
    }
    if (type == GGUF_TYPE_ARRAY) {
        uint32_t elem_type;
        uint64_t count;
        if (buf_read_u32(r, &elem_type)) return -1;
        if (buf_read_u64(r, &count)) return -1;
        for (uint64_t i = 0; i < count; i++) {
            if (skip_value(r, elem_type, depth + 1)) return -1;
        }
        return 0;
    }
    size_t sz = scalar_size(type);
    if (sz == 0) return -1;
    return buf_skip(r, sz);
}

/* ---- Public API ---- */

int gguf_vocab_extract(const void *gguf_data, size_t data_len, gguf_vocab_t *vocab)
{
    if (!gguf_data || data_len < 24 || !vocab) return -1;

    memset(vocab, 0, sizeof(*vocab));
    vocab->bos_id = -1;
    vocab->eos_id = -1;
    vocab->add_space_prefix = 1;  /* default: true (overridden by GGUF key if present) */

    buf_reader_t r = { .data = (const uint8_t *)gguf_data, .len = data_len, .pos = 0 };

    /* Read header */
    uint32_t magic, version;
    uint64_t n_tensors, n_kv;
    if (buf_read_u32(&r, &magic)) return -1;
    if (magic != GGUF_MAGIC) return -1;
    if (buf_read_u32(&r, &version)) return -1;
    if (version < 2 || version > 3) return -1;
    if (buf_read_u64(&r, &n_tensors)) return -1;
    if (buf_read_u64(&r, &n_kv)) return -1;
    if (n_kv > GGUF_MAX_KV_PAIRS) return -1;

    /* Walk KV pairs */
    char   **tokens_arr    = NULL;
    int32_t *token_types   = NULL;
    float   *real_scores   = NULL;
    char   **merges_arr    = NULL;
    uint64_t tokens_count  = 0;
    uint64_t types_count   = 0;
    uint64_t scores_count  = 0;
    uint64_t merges_count  = 0;
    int found_tokens = 0, found_types = 0, found_scores = 0, found_merges = 0;
    (void)found_types;  /* set during KV walk but no longer used for scoring */

    for (uint64_t kv_i = 0; kv_i < n_kv; kv_i++) {
        /* Read key */
        char key[512];
        if (buf_read_gguf_string_fixed(&r, key, sizeof(key))) goto fail;

        /* Read value type */
        uint32_t vtype;
        if (buf_read_u32(&r, &vtype)) goto fail;

        /* Check for keys of interest */
        if (vtype == GGUF_TYPE_UINT32 && strcmp(key, "tokenizer.ggml.bos_token_id") == 0) {
            uint32_t val;
            if (buf_read_u32(&r, &val)) goto fail;
            vocab->bos_id = (int)val;
            continue;
        }
        if (vtype == GGUF_TYPE_UINT32 && strcmp(key, "tokenizer.ggml.eos_token_id") == 0) {
            uint32_t val;
            if (buf_read_u32(&r, &val)) goto fail;
            vocab->eos_id = (int)val;
            continue;
        }

        if (vtype == GGUF_TYPE_BOOL && strcmp(key, "tokenizer.ggml.add_space_prefix") == 0) {
            uint8_t val;
            if (buf_read(&r, &val, 1)) goto fail;
            vocab->add_space_prefix = val ? 1 : 0;
            continue;
        }

        if (vtype == GGUF_TYPE_ARRAY && strcmp(key, "tokenizer.ggml.tokens") == 0) {
            /* Read string array */
            uint32_t elem_type;
            if (buf_read_u32(&r, &elem_type)) goto fail;
            if (buf_read_u64(&r, &tokens_count)) goto fail;
            if (elem_type != GGUF_TYPE_STRING) {
                /* Wrong element type, skip */
                for (uint64_t i = 0; i < tokens_count; i++) {
                    if (skip_value(&r, elem_type, 0)) goto fail;
                }
                continue;
            }
            /* Sanity check */
            if (tokens_count > 1000000) goto fail;

            tokens_arr = (char **)calloc((size_t)tokens_count, sizeof(char *));
            if (!tokens_arr) goto fail;

            for (uint64_t i = 0; i < tokens_count; i++) {
                tokens_arr[i] = buf_read_gguf_string_alloc(&r);
                if (!tokens_arr[i]) goto fail;
            }
            found_tokens = 1;
            continue;
        }

        if (vtype == GGUF_TYPE_ARRAY && strcmp(key, "tokenizer.ggml.token_type") == 0) {
            /* Read int32 array */
            uint32_t elem_type;
            if (buf_read_u32(&r, &elem_type)) goto fail;
            if (buf_read_u64(&r, &types_count)) goto fail;
            if (elem_type != GGUF_TYPE_INT32) {
                for (uint64_t i = 0; i < types_count; i++) {
                    if (skip_value(&r, elem_type, 0)) goto fail;
                }
                continue;
            }
            if (types_count > 1000000) goto fail;

            token_types = (int32_t *)malloc((size_t)types_count * sizeof(int32_t));
            if (!token_types) goto fail;

            for (uint64_t i = 0; i < types_count; i++) {
                if (buf_read_i32(&r, &token_types[i])) goto fail;
            }
            found_types = 1;
            continue;
        }

        if (vtype == GGUF_TYPE_ARRAY && strcmp(key, "tokenizer.ggml.scores") == 0) {
            /* Read float32 score array — BPE merge priorities */
            uint32_t elem_type;
            if (buf_read_u32(&r, &elem_type)) goto fail;
            if (buf_read_u64(&r, &scores_count)) goto fail;
            if (elem_type != GGUF_TYPE_FLOAT32) {
                for (uint64_t i = 0; i < scores_count; i++) {
                    if (skip_value(&r, elem_type, 0)) goto fail;
                }
                continue;
            }
            if (scores_count > 1000000) goto fail;

            real_scores = (float *)malloc((size_t)scores_count * sizeof(float));
            if (!real_scores) goto fail;

            for (uint64_t i = 0; i < scores_count; i++) {
                if (buf_read(&r, &real_scores[i], 4)) goto fail;
            }
            found_scores = 1;
            continue;
        }

        if (vtype == GGUF_TYPE_ARRAY && strcmp(key, "tokenizer.ggml.merges") == 0) {
            /* Read BPE merge rules — string array */
            uint32_t elem_type;
            if (buf_read_u32(&r, &elem_type)) goto fail;
            if (buf_read_u64(&r, &merges_count)) goto fail;
            if (elem_type != GGUF_TYPE_STRING) {
                for (uint64_t i = 0; i < merges_count; i++)
                    if (skip_value(&r, elem_type, 0)) goto fail;
                continue;
            }
            if (merges_count > 2000000) goto fail;

            merges_arr = (char **)calloc((size_t)merges_count, sizeof(char *));
            if (!merges_arr) goto fail;
            for (uint64_t i = 0; i < merges_count; i++) {
                merges_arr[i] = buf_read_gguf_string_alloc(&r);
                if (!merges_arr[i]) goto fail;
            }
            found_merges = 1;
            continue;
        }

        /* Skip value we don't care about */
        if (skip_value(&r, vtype, 0)) goto fail;
    }

    /* Validate we found the tokens array */
    if (!found_tokens || tokens_count == 0) goto fail;

    /* Build output */
    vocab->vocab_size = (int)tokens_count;
    vocab->tokens = tokens_arr;
    tokens_arr = NULL; /* transfer ownership */

    /* Assign scores. Priority: merges > GGUF scores > index fallback */
    vocab->scores = (float *)calloc((size_t)tokens_count, sizeof(float));
    if (!vocab->scores) goto fail;

    if (found_merges && merges_arr && merges_count > 0) {
        /* Merge-rank scoring: for each merge "A B", concatenate to "AB",
         * look up in tokens, assign score = -(float)merge_index.
         * Earlier merges get higher (less negative) scores = merged first. */
        vocab->has_merges = 1;

        /* Build temporary hash table for fast token→index lookup */
        int ht_cap = 1;
        while (ht_cap < (int)tokens_count * 2) ht_cap <<= 1;
        int *ht = (int *)malloc(sizeof(int) * (size_t)ht_cap);
        if (!ht) goto fail;
        memset(ht, 0xFF, sizeof(int) * (size_t)ht_cap);  /* fill with -1 */
        for (int i = 0; i < (int)tokens_count; i++) {
            uint64_t h = fnv1a_hash(vocab->tokens[i]) % (uint64_t)ht_cap;
            while (ht[h % (uint64_t)ht_cap] != -1)
                h = (h + 1) % (uint64_t)ht_cap;
            ht[h] = i;
        }

        /* Start with very low default score for all tokens */
        for (uint64_t i = 0; i < tokens_count; i++)
            vocab->scores[i] = -1e9f;

        /* For each merge "A B", concat → "AB", look up, assign rank score */
        char merge_buf[256];
        int merges_matched = 0;
        for (uint64_t mi = 0; mi < merges_count; mi++) {
            const char *m = merges_arr[mi];
            const char *sep = strchr(m, ' ');
            if (!sep) continue;

            size_t a_len = (size_t)(sep - m);
            size_t b_len = strlen(sep + 1);
            if (a_len + b_len >= sizeof(merge_buf)) continue;

            memcpy(merge_buf, m, a_len);
            memcpy(merge_buf + a_len, sep + 1, b_len);
            merge_buf[a_len + b_len] = '\0';

            /* Look up merged token in hash table */
            uint64_t h = fnv1a_hash(merge_buf) % (uint64_t)ht_cap;
            int probes = ht_cap;
            while (ht[h] != -1 && probes-- > 0) {
                if (strcmp(vocab->tokens[ht[h]], merge_buf) == 0) {
                    vocab->scores[ht[h]] = -(float)mi;
                    merges_matched++;
                    break;
                }
                h = (h + 1) % (uint64_t)ht_cap;
            }
        }

        free(ht);
        printf("[vocab] merges: %llu total, %d matched to tokens\n",
               (unsigned long long)merges_count, merges_matched);

        /* Free merge strings */
        for (uint64_t i = 0; i < merges_count; i++)
            free(merges_arr[i]);
        free(merges_arr);
        merges_arr = NULL;

    } else if (found_scores && scores_count == tokens_count && real_scores) {
        /* Real scores from tokenizer.ggml.scores */
        memcpy(vocab->scores, real_scores, (size_t)tokens_count * sizeof(float));
    } else {
        /* Fallback: index-based (works for GPT-2 BPE / Llama) */
        for (uint64_t i = 0; i < tokens_count; i++)
            vocab->scores[i] = -(float)i;
    }

    free(token_types);
    free(real_scores);
    return 0;

fail:
    /* Clean up on error */
    if (tokens_arr) {
        for (uint64_t i = 0; i < tokens_count; i++)
            free(tokens_arr[i]);
        free(tokens_arr);
    }
    free(token_types);
    free(real_scores);
    if (merges_arr) {
        for (uint64_t i = 0; i < merges_count; i++)
            free(merges_arr[i]);
        free(merges_arr);
    }
    if (vocab->tokens) {
        for (int i = 0; i < vocab->vocab_size; i++)
            free(vocab->tokens[i]);
        free(vocab->tokens);
        vocab->tokens = NULL;
    }
    free(vocab->scores);
    vocab->scores = NULL;
    vocab->vocab_size = 0;
    return -1;
}

void gguf_vocab_free(gguf_vocab_t *vocab)
{
    if (!vocab) return;
    if (vocab->tokens) {
        for (int i = 0; i < vocab->vocab_size; i++)
            free(vocab->tokens[i]);
        free(vocab->tokens);
        vocab->tokens = NULL;
    }
    if (vocab->scores) {
        free(vocab->scores);
        vocab->scores = NULL;
    }
    vocab->vocab_size = 0;
}

int gguf_vocab_init_tokenizer(const gguf_vocab_t *vocab, tokenizer_t *tok)
{
    if (!vocab || !tok || vocab->vocab_size <= 0 || !vocab->tokens)
        return -1;

    int rc = tokenizer_init(tok, (const char **)vocab->tokens, vocab->scores,
                            vocab->vocab_size, vocab->bos_id, vocab->eos_id);
    if (rc == 0)
        tok->add_space_prefix = vocab->add_space_prefix;
    return rc;
}

/* Legacy stub API — preserved for CMakeLists.txt compatibility */
int gguf_vocab_init(const gguf_ctx_t *ctx, tokenizer_t *tok)
{
    (void)ctx;
    (void)tok;
    /* Use gguf_vocab_extract() + gguf_vocab_init_tokenizer() instead */
    return -1;
}
