/*
 * JARVIS AI-OS - DMA Memory Allocator Implementation
 *
 * Allocates physically-contiguous memory for DMA operations.
 * Tracks physical addresses by allocating from untyped memory.
 *
 * Key insight: seL4 has no API to get paddr from a mapped frame.
 * The ONLY way to know paddr is to track it when retyping from untyped.
 *
 * This solves the ADMA2 addressing issue discovered in Week 36 hardware tests:
 * - ELF-loader proved vaddr != paddr (not identity mapped)
 * - Static buffers have unknown paddr
 * - DMA engine requires real physical addresses
 */

#include "dma_alloc.h"
#include "slot_alloc.h"
#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <string.h>

/* BIT macro */
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

/* Debug output via kernel */
static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex(seL4_Word val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xF]);
    }
}

/* DMA allocator state */
static seL4_CPtr dma_untyped = seL4_CapNull;
static seL4_Word dma_untyped_paddr = 0;
static seL4_Word dma_untyped_size = 0;
static seL4_Word dma_alloc_offset = 0;
static seL4_Word dma_vaddr_base = 0x5c4000;  /* Within VSpace [0x400000..0x609fff], after EMMC at 0x5c2000 */
/* NOTE: Slot allocation now uses shared slot_alloc module */
static bool dma_initialized = false;

/* Track allocated regions for paddr lookup */
static struct {
    void *vaddr;
    uintptr_t paddr;
    size_t size;
} dma_regions[DMA_MAX_REGIONS];
static int dma_region_count = 0;

/*
 * Find the first regular (non-device) untyped >= min_size
 */
static seL4_CPtr find_regular_untyped(seL4_BootInfo *bi, size_t min_size,
                                       seL4_Word *paddr_out, int *size_bits_out)
{
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        seL4_UntypedDesc *ud = &bi->untypedList[i];

        /* Skip device memory - we need normal RAM for DMA */
        if (ud->isDevice) {
            continue;
        }

        seL4_Word size = BIT(ud->sizeBits);

        /* Check if large enough */
        if (size >= min_size) {
            if (paddr_out) {
                *paddr_out = ud->paddr;
            }
            if (size_bits_out) {
                *size_bits_out = ud->sizeBits;
            }
            return bi->untyped.start + i;
        }
    }
    return seL4_CapNull;
}

bool dma_alloc_init(seL4_BootInfo *bi)
{
    if (dma_initialized) {
        return true;
    }

    if (!bi) {
        bi = sel4runtime_bootinfo();
    }
    if (!bi) {
        debug_puts("[DMA] ERROR: bootinfo is NULL\n");
        return false;
    }

    /* Find a regular untyped >= 256KB */
    seL4_Word paddr = 0;
    int size_bits = 0;
    seL4_CPtr ut = find_regular_untyped(bi, DMA_POOL_SIZE, &paddr, &size_bits);

    if (ut == seL4_CapNull) {
        debug_puts("[DMA] ERROR: No regular untyped >= 256KB found\n");
        debug_puts("[DMA] Scanning untypeds:\n");

        /* Debug: list all untypeds */
        for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start && i < 10; i++) {
            seL4_UntypedDesc *ud = &bi->untypedList[i];
            debug_puts("  [");
            debug_hex(i);
            debug_puts("] paddr=");
            debug_hex(ud->paddr);
            debug_puts(" size=");
            debug_hex(BIT(ud->sizeBits));
            debug_puts(ud->isDevice ? " DEVICE\n" : " RAM\n");
        }
        return false;
    }

    /* Verify shared slot allocator is ready */
    if (!slot_alloc_ready()) {
        debug_puts("[DMA] ERROR: slot_alloc not initialized\n");
        return false;
    }

    dma_untyped = ut;
    dma_untyped_paddr = paddr;
    dma_untyped_size = BIT(size_bits);
    dma_alloc_offset = 0;
    /* Slots now come from shared slot_alloc module */
    dma_region_count = 0;
    dma_initialized = true;

    debug_puts("[DMA] Found regular untyped: paddr=");
    debug_hex(dma_untyped_paddr);
    debug_puts(" size=");
    debug_hex(dma_untyped_size);
    debug_puts("\n");

    return true;
}

void *dma_alloc(size_t size, uintptr_t *paddr_out)
{
    if (!dma_initialized) {
        debug_puts("[DMA] ERROR: not initialized\n");
        return NULL;
    }

    if (size == 0) {
        debug_puts("[DMA] ERROR: size is 0\n");
        return NULL;
    }

    /* Round up to page size */
    size_t pages = (size + DMA_PAGE_SIZE_4K - 1) / DMA_PAGE_SIZE_4K;
    size_t alloc_size = pages * DMA_PAGE_SIZE_4K;

    /* Check pool space */
    if (dma_alloc_offset + alloc_size > DMA_POOL_SIZE) {
        debug_puts("[DMA] ERROR: Pool exhausted (requested ");
        debug_hex(alloc_size);
        debug_puts(" bytes, ");
        debug_hex(DMA_POOL_SIZE - dma_alloc_offset);
        debug_puts(" remaining)\n");
        return NULL;
    }

    /* Check tracking space */
    if (dma_region_count >= DMA_MAX_REGIONS) {
        debug_puts("[DMA] ERROR: Too many regions\n");
        return NULL;
    }

    uintptr_t result_paddr = dma_untyped_paddr + dma_alloc_offset;
    uintptr_t result_vaddr = dma_vaddr_base + dma_alloc_offset;

    debug_puts("[DMA] Allocating ");
    debug_hex(alloc_size);
    debug_puts(" bytes (");
    debug_hex(pages);
    debug_puts(" pages)\n");

    /* Allocate and map each page */
    for (size_t p = 0; p < pages; p++) {
        seL4_CPtr frame_cap = slot_alloc_next("DMA frame");
        if (frame_cap == seL4_CapNull) {
            debug_puts("[DMA] Slot allocation failed at page ");
            debug_hex(p);
            debug_puts("\n");
            return NULL;
        }

        /* Retype untyped -> ARM SmallPage (4KB) */
        seL4_Error err = seL4_Untyped_Retype(
            dma_untyped,
            seL4_ARM_SmallPageObject,
            DMA_PAGE_BITS_4K,
            seL4_CapInitThreadCNode,
            0,  /* node_index */
            0,  /* node_depth */
            frame_cap,
            1   /* num_objects */
        );

        if (err != seL4_NoError) {
            debug_puts("[DMA] Retype failed at page ");
            debug_hex(p);
            debug_puts(": error ");
            debug_hex(err);
            debug_puts("\n");
            return NULL;
        }

        /* Map to vaddr in init thread's VSpace */
        seL4_Word page_vaddr = result_vaddr + (p * DMA_PAGE_SIZE_4K);

        /*
         * Map as UNCACHEABLE (vm_attributes = 0) for DMA correctness.
         *
         * seL4 runs the rootserver at EL0 where ALL cache maintenance
         * instructions (DC IVAC, DC CIVAC, DC CVAC) trap to EL1.
         * Instead of fighting the cache, map DMA buffers uncacheable
         * so CPU writes go directly to RAM and DMA reads see them.
         */
        err = seL4_ARM_Page_Map(
            frame_cap,
            seL4_CapInitThreadVSpace,
            page_vaddr,
            seL4_AllRights,
            0  /* uncacheable - required for DMA */
        );

        if (err != seL4_NoError) {
            debug_puts("[DMA] Map failed at page ");
            debug_hex(p);
            debug_puts(" vaddr=");
            debug_hex(page_vaddr);
            debug_puts(": error ");
            debug_hex(err);
            debug_puts("\n");

            /* Error 6 = seL4_FailedLookup - vaddr not in valid range */
            if (err == 6) {
                debug_puts("[DMA] Hint: vaddr ");
                debug_hex(page_vaddr);
                debug_puts(" may be outside VSpace range\n");
            }
            return NULL;
        }
    }

    /* Track region */
    dma_regions[dma_region_count].vaddr = (void *)result_vaddr;
    dma_regions[dma_region_count].paddr = result_paddr;
    dma_regions[dma_region_count].size = alloc_size;
    dma_region_count++;

    /* Update allocator state */
    dma_alloc_offset += alloc_size;

    /* SEC-015: Warn when DMA pool utilization exceeds 80% */
    if (dma_alloc_offset * 5 > DMA_POOL_SIZE * 4) {
        debug_puts("[DMA] WARNING: pool >80% utilized (");
        debug_hex(dma_alloc_offset);
        debug_puts("/");
        debug_hex(DMA_POOL_SIZE);
        debug_puts(")\n");
    }
    /* NOTE: DMA freeing not implemented. seL4 untyped retypes are forward-only —
     * once consumed, frames cannot be returned. See SEC-015. */

    debug_puts("[DMA] Allocated: vaddr=");
    debug_hex(result_vaddr);
    debug_puts(" paddr=");
    debug_hex(result_paddr);
    debug_puts(" size=");
    debug_hex(alloc_size);
    debug_puts("\n");

    if (paddr_out) {
        *paddr_out = result_paddr;
    }

    /* Zero the memory for safety */
    memset((void *)result_vaddr, 0, alloc_size);

    return (void *)result_vaddr;
}

uintptr_t dma_get_paddr(void *vaddr)
{
    for (int i = 0; i < dma_region_count; i++) {
        uintptr_t start = (uintptr_t)dma_regions[i].vaddr;
        uintptr_t end = start + dma_regions[i].size;

        if ((uintptr_t)vaddr >= start && (uintptr_t)vaddr < end) {
            /* Calculate offset within region */
            uintptr_t offset = (uintptr_t)vaddr - start;
            return dma_regions[i].paddr + offset;
        }
    }

    debug_puts("[DMA] WARNING: paddr lookup failed for vaddr=");
    debug_hex((uintptr_t)vaddr);
    debug_puts("\n");
    return 0;  /* Not found */
}

bool dma_alloc_is_ready(void)
{
    return dma_initialized;
}

size_t dma_alloc_remaining(void)
{
    if (!dma_initialized) {
        return 0;
    }
    return DMA_POOL_SIZE - dma_alloc_offset;
}
