/*
 * JARVIS AI-OS Phase 3 — Debug Reference Values
 * Dequantizes specific rows from the real GGUF model to provide
 * ground truth for comparing against QEMU rootserver output.
 *
 * Usage: ./debug_ref <path_to_gguf>
 */

#include "gguf_parser.h"
#include "dequant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void print_hex(uint32_t val) {
    printf("0x%08x", val);
}

static void print_float_hex(float f) {
    union { float f; uint32_t u; } conv;
    conv.f = f;
    printf("%12.6f (", f);
    print_hex(conv.u);
    printf(")");
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] :
        "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf";

    printf("=== JARVIS Debug Reference Values ===\n\n");
    printf("Model: %s\n\n", path);

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, path);
    if (err) {
        printf("FAIL: gguf_open: %s\n", gguf_strerror(err));
        return 1;
    }

    printf("Version: %u, Tensors: %lu, KV: %lu\n\n",
           ctx.version, (unsigned long)ctx.n_tensors, (unsigned long)ctx.n_kv);

    /* ---- token_embd.weight ---- */
    const gguf_tensor_info_t *embd = gguf_find_tensor(&ctx, "token_embd.weight");
    if (!embd) { printf("FAIL: token_embd.weight not found\n"); return 1; }

    printf("token_embd.weight:\n");
    printf("  type: %s (%u)\n", gguf_type_name(embd->type), embd->type);
    printf("  n_elements: %lu\n", (unsigned long)embd->n_elements);
    printf("  n_bytes: %lu\n", (unsigned long)embd->n_bytes);
    printf("  dims: [%lu, %lu]\n", (unsigned long)embd->dims[0], (unsigned long)embd->dims[1]);

    int dim = (int)embd->dims[0];
    int vocab = (int)(embd->n_elements / dim);
    size_t row_bytes = dequant_row_bytes(dim, (ggml_type_t)embd->type);
    printf("  dim: %d, vocab: %d, row_bytes: %zu\n", dim, vocab, row_bytes);
    printf("  block_size: %d, block_bytes: %zu\n",
           dequant_type_block_size((ggml_type_t)embd->type),
           dequant_type_block_bytes((ggml_type_t)embd->type));

    /* Read the raw tensor data */
    void *embd_raw = malloc((size_t)embd->n_bytes);
    if (!embd_raw) { printf("FAIL: malloc\n"); return 1; }
    err = gguf_read_tensor_data(&ctx, embd, embd_raw);
    if (err) { printf("FAIL: read tensor data: %s\n", gguf_strerror(err)); return 1; }

    /* Dequant row 9906 (token for "Hello") */
    float row_out[4096];
    const uint8_t *row_ptr = (const uint8_t *)embd_raw + (size_t)9906 * row_bytes;
    dequant_row(row_ptr, row_out, dim, (ggml_type_t)embd->type);

    printf("\n  Row 9906 ('Hello' token) first 8 values:\n");
    for (int i = 0; i < 8; i++) {
        printf("    [%d] ", i);
        print_float_hex(row_out[i]);
        printf("\n");
    }

    /* Also row 0 for sanity */
    const uint8_t *row0_ptr = (const uint8_t *)embd_raw;
    dequant_row(row0_ptr, row_out, dim, (ggml_type_t)embd->type);
    printf("\n  Row 0 first 8 values:\n");
    for (int i = 0; i < 8; i++) {
        printf("    [%d] ", i);
        print_float_hex(row_out[i]);
        printf("\n");
    }

    free(embd_raw);

    /* ---- blk.0.attn_q.weight ---- */
    const gguf_tensor_info_t *wq = gguf_find_tensor(&ctx, "blk.0.attn_q.weight");
    if (!wq) { printf("FAIL: blk.0.attn_q.weight not found\n"); return 1; }

    printf("\nblk.0.attn_q.weight:\n");
    printf("  type: %s (%u)\n", gguf_type_name(wq->type), wq->type);
    printf("  n_elements: %lu\n", (unsigned long)wq->n_elements);
    printf("  n_bytes: %lu\n", (unsigned long)wq->n_bytes);
    printf("  dims: [%lu, %lu]\n", (unsigned long)wq->dims[0], (unsigned long)wq->dims[1]);

    int wq_cols = (int)wq->dims[0];
    size_t wq_row_bytes = dequant_row_bytes(wq_cols, (ggml_type_t)wq->type);
    printf("  cols: %d, row_bytes: %zu\n", wq_cols, wq_row_bytes);
    printf("  block_size: %d, block_bytes: %zu\n",
           dequant_type_block_size((ggml_type_t)wq->type),
           dequant_type_block_bytes((ggml_type_t)wq->type));

    /* Read raw data */
    void *wq_raw = malloc((size_t)wq->n_bytes);
    if (!wq_raw) { printf("FAIL: malloc\n"); return 1; }
    err = gguf_read_tensor_data(&ctx, wq, wq_raw);
    if (err) { printf("FAIL: read wq data: %s\n", gguf_strerror(err)); return 1; }

    /* Dequant row 0 */
    dequant_row(wq_raw, row_out, wq_cols, (ggml_type_t)wq->type);
    printf("\n  Row 0 first 8 values:\n");
    for (int i = 0; i < 8; i++) {
        printf("    [%d] ", i);
        print_float_hex(row_out[i]);
        printf("\n");
    }

    free(wq_raw);

    /* ---- output.weight (may not exist — weight tying) ---- */
    const gguf_tensor_info_t *out_w = gguf_find_tensor(&ctx, "output.weight");
    if (out_w) {
        printf("\noutput.weight: EXISTS (type %s)\n", gguf_type_name(out_w->type));
    } else {
        printf("\noutput.weight: NOT FOUND — model uses weight tying (shares token_embd)\n");
    }

    /* ---- output_norm.weight ---- */
    const gguf_tensor_info_t *onorm = gguf_find_tensor(&ctx, "output_norm.weight");
    if (onorm) {
        printf("\noutput_norm.weight:\n");
        printf("  type: %s, n_elements: %lu\n",
               gguf_type_name(onorm->type), (unsigned long)onorm->n_elements);
    }

    gguf_close(&ctx);
    printf("\n=== Reference values generated ===\n");
    return 0;
}
