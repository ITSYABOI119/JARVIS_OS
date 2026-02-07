/*
 * JARVIS AI-OS - Shared Capability Slot Allocator
 *
 * Week 36 Fix: All subsystems (UART, DMA, etc.) must use this single
 * allocator to avoid slot collisions. Previously each had its own
 * cursor starting at bi->empty.start, causing "Slot #N non-empty" errors.
 */

#ifndef SLOT_ALLOC_H
#define SLOT_ALLOC_H

#include <sel4/sel4.h>
#include <stdbool.h>

/*
 * Initialize the shared slot allocator.
 * Must be called once, early in main(), before any device/DMA init.
 *
 * @param bi  Bootinfo pointer (provides empty slot range)
 * @return    true on success, false if already initialized or bi is NULL
 */
bool slot_alloc_init(seL4_BootInfo *bi);

/*
 * Allocate the next available capability slot.
 * Thread-safe for single-threaded rootserver.
 *
 * @param purpose  Debug string describing what the slot is for (e.g., "UART frame")
 * @return         Capability slot, or seL4_CapNull if exhausted
 */
seL4_CPtr slot_alloc_next(const char *purpose);

/*
 * Check if slot allocator is initialized.
 */
bool slot_alloc_ready(void);

/*
 * Get number of remaining slots.
 */
seL4_Word slot_alloc_remaining(void);

#endif /* SLOT_ALLOC_H */
