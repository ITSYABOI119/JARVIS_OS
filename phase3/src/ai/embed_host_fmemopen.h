/*
 * embed_host_fmemopen.h — Phase C / C/M1a Stage 2 host-build shim (Windows only, model-gated-local).
 *
 * The MSVC CRT (clang targeting Windows) has NO fmemopen (a POSIX extension), which gguf_parser.c's
 * gguf_open_memory() calls. This header just DECLARES it so every TU of the vector-parity host build
 * compiles under clang/Windows; the DEFINITION lives in embed_vector_probe.c (the leaf harness, where
 * <windows.h> can be pulled in without polluting the engine TUs with its min/max/ERROR macros).
 *
 * Force-included via `-include phase3/src/ai/embed_host_fmemopen.h`. On non-Windows (Linux/gcc) this
 * is empty and the real POSIX fmemopen is used. NOT part of the deployed seL4 build or CI — a
 * host-parity-harness support header only (the model is model-gated-local, so is this).
 */
#ifndef EMBED_HOST_FMEMOPEN_H
#define EMBED_HOST_FMEMOPEN_H
#if defined(_WIN32)
#include <stdio.h>   /* FILE */
#include <stddef.h>  /* size_t */
FILE *fmemopen(void *buf, size_t size, const char *mode);
#endif  /* _WIN32 */
#endif  /* EMBED_HOST_FMEMOPEN_H */
