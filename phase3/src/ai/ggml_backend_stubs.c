/*
 * JARVIS AI-OS -- ggml Backend Stubs for seL4 Bare Metal
 *
 * Provides minimal stub implementations of ggml backend and threading
 * functions needed to link ggml.o + ggml-alloc.o for basic tensor ops.
 *
 * These are NOT needed if using ggml-cpu backend (which provides real
 * implementations). They exist solely for testing that the core tensor
 * library (ggml.c) works with our POSIX stubs.
 *
 * On seL4 bare metal, the real backend will be a custom CPU-only backend
 * that maps to our memory allocator and single-threaded compute.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* --- Threading stubs (from ggml-threading.h) --- */

void ggml_critical_section_start(void)
{
    /* Single-threaded on seL4 bare metal: no-op */
}

void ggml_critical_section_end(void)
{
    /* Single-threaded on seL4 bare metal: no-op */
}

/* --- Backend buffer stubs (from ggml-backend.h) --- */

/* These are called by ggml-alloc.c for buffer management.
 * On seL4, we use a simple flat buffer model. */

void ggml_backend_tensor_memset(void *tensor, uint8_t value,
                                size_t offset, size_t size)
{
    /* Stub: real impl sets tensor data in backend buffer.
     * For CPU-only, this is just memset on tensor->data + offset. */
    (void)tensor; (void)value; (void)offset; (void)size;
}

void ggml_backend_tensor_set(void *tensor, const void *data,
                             size_t offset, size_t size)
{
    /* Stub: real impl copies data to tensor backend buffer. */
    (void)tensor; (void)data; (void)offset; (void)size;
}

/* ggml-alloc.o backend buffer stubs */
void *ggml_backend_buffer_get_base(void *buffer)
{
    (void)buffer; return NULL;
}

size_t ggml_backend_buffer_get_size(void *buffer)
{
    (void)buffer; return 0;
}

size_t ggml_backend_buffer_get_alignment(void *buffer)
{
    (void)buffer; return 32;
}

size_t ggml_backend_buffer_get_alloc_size(void *buffer, void *tensor)
{
    (void)buffer; (void)tensor; return 0;
}

void ggml_backend_buffer_free(void *buffer)
{
    (void)buffer;
}

void ggml_backend_buffer_reset(void *buffer)
{
    (void)buffer;
}

void ggml_backend_buffer_set_usage(void *buffer, int usage)
{
    (void)buffer; (void)usage;
}

void *ggml_backend_buft_alloc_buffer(void *buft, size_t size)
{
    (void)buft; (void)size; return NULL;
}

size_t ggml_backend_buft_get_alloc_size(void *buft, void *tensor)
{
    (void)buft; (void)tensor; return 0;
}

size_t ggml_backend_buft_get_alignment(void *buft)
{
    (void)buft; return 32;
}

size_t ggml_backend_buft_get_max_size(void *buft)
{
    (void)buft; return 0;
}

const char *ggml_backend_buft_name(void *buft)
{
    (void)buft; return "stub";
}

void *ggml_backend_get_default_buffer_type(void *backend)
{
    (void)backend; return NULL;
}

void *ggml_backend_multi_buffer_alloc_buffer(void *buft, size_t size)
{
    (void)buft; (void)size; return NULL;
}

void ggml_backend_tensor_alloc(void *buffer, void *tensor, void *addr)
{
    (void)buffer; (void)tensor; (void)addr;
}

void ggml_backend_view_init(void *tensor)
{
    (void)tensor;
}
