/*
 * JARVIS AI-OS - DMA Memory Allocator
 *
 * Allocates physically-contiguous memory for DMA operations.
 * Tracks physical addresses by allocating from untyped memory.
 *
 * Key insight: seL4 has no API to get paddr from a mapped frame.
 * The ONLY way to know paddr is to track it when retyping from untyped.
 */

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sel4/sel4.h>

/* DMA pool size - 256KB should be enough for EMMC ADMA2 */
#define DMA_POOL_SIZE       (256 * 1024)

/* Page size constants */
#define DMA_PAGE_SIZE_4K    4096
#define DMA_PAGE_BITS_4K    12

/* Maximum number of tracked DMA regions */
#define DMA_MAX_REGIONS     32

/*
 * Initialize DMA allocator.
 * Finds a regular (non-device) untyped >= 256KB in bootinfo.
 * Must be called early in main(), before any DMA operations.
 *
 * Returns: true on success, false if no suitable untyped found.
 */
bool dma_alloc_init(seL4_BootInfo *bi);

/*
 * Allocate contiguous DMA buffer.
 * Allocates from the untyped pool and maps to a fixed vaddr range.
 *
 * @param size      Bytes to allocate (rounded up to page size)
 * @param paddr_out Output: physical address for DMA engine
 *
 * Returns: virtual address for CPU access, or NULL on failure.
 *          The paddr_out is set to the physical address.
 */
void *dma_alloc(size_t size, uintptr_t *paddr_out);

/*
 * Get physical address for a previously allocated DMA buffer.
 * Looks up the vaddr in the tracked regions.
 *
 * @param vaddr Virtual address within a DMA region
 *
 * Returns: physical address, or 0 if not found.
 */
uintptr_t dma_get_paddr(void *vaddr);

/*
 * Check if DMA allocator is initialized.
 */
bool dma_alloc_is_ready(void);

/*
 * Get remaining DMA pool space in bytes.
 */
size_t dma_alloc_remaining(void);

#endif /* DMA_ALLOC_H */
