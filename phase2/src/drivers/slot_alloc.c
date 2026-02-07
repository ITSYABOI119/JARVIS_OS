/*
 * JARVIS AI-OS - Shared Capability Slot Allocator
 *
 * Week 36 Fix: Centralized slot allocation to prevent collisions.
 * All subsystems (UART device mapper, DMA allocator) must use this.
 */

#include "slot_alloc.h"
#include <sel4/sel4.h>

/* Allocator state */
static bool g_slot_alloc_ready = false;
static seL4_CPtr g_slot_next = seL4_CapNull;
static seL4_CPtr g_slot_end = seL4_CapNull;

/* Debug output via kernel */
static void slot_debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void slot_debug_hex(seL4_Word val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xF]);
    }
}

bool slot_alloc_init(seL4_BootInfo *bi)
{
    if (g_slot_alloc_ready) {
        slot_debug_puts("[SLOT] Already initialized\n");
        return true;
    }

    if (!bi) {
        slot_debug_puts("[SLOT] ERROR: bootinfo is NULL\n");
        return false;
    }

    if (bi->empty.start >= bi->empty.end) {
        slot_debug_puts("[SLOT] ERROR: No empty slots in bootinfo\n");
        return false;
    }

    g_slot_next = bi->empty.start;
    g_slot_end = bi->empty.end;
    g_slot_alloc_ready = true;

    slot_debug_puts("[SLOT] Init: start=");
    slot_debug_hex(g_slot_next);
    slot_debug_puts(" end=");
    slot_debug_hex(g_slot_end);
    slot_debug_puts(" (");
    slot_debug_hex(g_slot_end - g_slot_next);
    slot_debug_puts(" slots)\n");

    return true;
}

seL4_CPtr slot_alloc_next(const char *purpose)
{
    if (!g_slot_alloc_ready) {
        slot_debug_puts("[SLOT] ERROR: Not initialized\n");
        return seL4_CapNull;
    }

    if (g_slot_next >= g_slot_end) {
        slot_debug_puts("[SLOT] ERROR: Exhausted\n");
        return seL4_CapNull;
    }

    seL4_CPtr slot = g_slot_next++;

    slot_debug_puts("[SLOT] Alloc #");
    slot_debug_hex(slot);
    slot_debug_puts(" for ");
    slot_debug_puts(purpose ? purpose : "?");
    slot_debug_puts("\n");

    return slot;
}

bool slot_alloc_ready(void)
{
    return g_slot_alloc_ready;
}

seL4_Word slot_alloc_remaining(void)
{
    if (!g_slot_alloc_ready) {
        return 0;
    }
    return g_slot_end - g_slot_next;
}
