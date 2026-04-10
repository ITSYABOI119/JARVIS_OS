/*
 * JARVIS AI-OS Phase 3 -- GGUF Vocabulary Extractor
 *
 * Reads tokenizer vocab directly from raw GGUF binary data.
 * The standard gguf_parser skips array data; this module walks
 * the binary to extract the specific arrays we need:
 *   tokenizer.ggml.tokens     (arr[str])
 *   tokenizer.ggml.token_type (arr[i32]) -- used as score proxy
 *   tokenizer.ggml.bos_token_id (u32)
 *   tokenizer.ggml.eos_token_id (u32)
 */

#ifndef GGUF_VOCAB_H
#define GGUF_VOCAB_H

#include "gguf_parser.h"
#include "tokenizer.h"
#include <stddef.h>

/* Extracted vocabulary from GGUF metadata */
typedef struct {
    char  **tokens;      /* Array of vocab_size token strings (malloc'd) */
    float  *scores;      /* Array of vocab_size scores (malloc'd) */
    int     vocab_size;
    int     bos_id;
    int     eos_id;
    int     has_merges;  /* true if tokenizer.ggml.merges was found and used for scoring */
} gguf_vocab_t;

/**
 * Extract tokenizer vocabulary from raw GGUF data (memory buffer).
 * Walks the GGUF KV pairs to find tokenizer arrays.
 * Allocates tokens and scores arrays -- caller must free with gguf_vocab_free().
 * Returns 0 on success, -1 on error.
 */
int gguf_vocab_extract(const void *gguf_data, size_t data_len, gguf_vocab_t *vocab);

/**
 * Free vocabulary data allocated by gguf_vocab_extract().
 */
void gguf_vocab_free(gguf_vocab_t *vocab);

/**
 * Initialize a tokenizer_t from extracted vocab.
 * Returns 0 on success, -1 on error.
 */
int gguf_vocab_init_tokenizer(const gguf_vocab_t *vocab, tokenizer_t *tok);

/**
 * Legacy API: extract vocab from parsed GGUF context and initialize tokenizer.
 * Stub -- returns -1 (use gguf_vocab_extract + gguf_vocab_init_tokenizer instead).
 */
int gguf_vocab_init(const gguf_ctx_t *ctx, tokenizer_t *tok);

#endif /* GGUF_VOCAB_H */
