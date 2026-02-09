/*
 * JARVIS AI-OS - Minimal FDT (Flattened Device Tree) Parser
 *
 * Week 45: Parses embedded DTB blob to extract device addresses and properties.
 * No dynamic allocation -- operates directly on the raw DTB using pointer arithmetic.
 *
 * All public symbols prefixed with jarvis_ to avoid collision with system libfdt.
 *
 * FDT format: https://www.devicetree.org/specifications/
 * All multi-byte values are big-endian in the DTB.
 */

#ifndef JARVIS_FDT_PARSER_H
#define JARVIS_FDT_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* FDT magic number (big-endian in blob) */
#define JARVIS_FDT_MAGIC       0xD00DFEED

/* FDT structure block tokens */
#define JARVIS_FDT_BEGIN_NODE  0x01
#define JARVIS_FDT_END_NODE    0x02
#define JARVIS_FDT_PROP        0x03
#define JARVIS_FDT_NOP         0x04
#define JARVIS_FDT_END         0x09

/* FDT header (40 bytes, all fields big-endian) */
struct jarvis_fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

/* Result of a property lookup */
struct jarvis_fdt_prop_result {
    const uint8_t *data;    /* Pointer into DTB blob (big-endian!) */
    uint32_t len;           /* Length in bytes */
    bool found;
};

/* Result of a reg property lookup */
struct jarvis_fdt_reg_result {
    uint64_t base;          /* Base address (host-endian) */
    uint64_t size;          /* Region size (host-endian) */
    bool found;
};

/*
 * Initialize FDT parser with DTB blob address.
 * Validates magic number and basic header fields.
 * Returns 0 on success, -1 on error.
 */
int jarvis_fdt_init(const void *dtb_blob);

/*
 * Check if FDT parser has been initialized with a valid DTB.
 */
bool jarvis_fdt_is_valid(void);

/*
 * Get the total size of the DTB blob.
 */
uint32_t jarvis_fdt_totalsize(void);

/*
 * Find a property by node path and property name.
 * Path uses '/' separators, e.g. "/soc/serial@fe201000"
 * Returns jarvis_fdt_prop_result with found=true if property exists.
 *
 * Example: jarvis_fdt_get_prop("/soc/serial@fe201000", "compatible")
 */
struct jarvis_fdt_prop_result jarvis_fdt_get_prop(const char *path, const char *propname);

/*
 * Get the "reg" property of a node as (base, size).
 * Handles #address-cells=2, #size-cells=1 (our standard layout).
 * Returns jarvis_fdt_reg_result with found=true if reg property exists.
 *
 * Example: jarvis_fdt_get_reg("/soc/serial@fe201000") -> {0xfe201000, 0x1000}
 */
struct jarvis_fdt_reg_result jarvis_fdt_get_reg(const char *path);

/*
 * Read a uint32 property value (first cell only).
 * Returns the value in host-endian, or default_val if not found.
 *
 * Example: jarvis_fdt_get_u32("/soc/watchdog@fe100000", "jarvis,timeout-sec", 0)
 */
uint32_t jarvis_fdt_get_u32(const char *path, const char *propname, uint32_t default_val);

/*
 * Get a string property value.
 * Returns pointer to null-terminated string in DTB, or NULL if not found.
 *
 * Example: jarvis_fdt_get_string("/", "model")
 */
const char *jarvis_fdt_get_string(const char *path, const char *propname);

/*
 * Count number of child nodes under a given path.
 * Returns 0 if path not found.
 *
 * Example: jarvis_fdt_count_children("/soc") -> number of peripheral nodes
 */
int jarvis_fdt_count_children(const char *path);

#endif /* JARVIS_FDT_PARSER_H */
