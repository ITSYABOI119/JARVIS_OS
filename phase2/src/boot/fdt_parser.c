/*
 * JARVIS AI-OS - Minimal FDT Parser Implementation
 *
 * Week 45: ~400 LOC, no dynamic allocation.
 * Parses embedded DTB blob to extract device addresses and properties.
 * Operates directly on the raw DTB using pointer arithmetic only.
 *
 * All multi-byte values in FDT are big-endian. ARM64 is little-endian,
 * so every field read from the blob must be byte-swapped.
 *
 * All public symbols prefixed with jarvis_ to avoid collision with system libfdt.
 */

#include "fdt_parser.h"
#include <sel4/sel4.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Debug helpers                                                       */
/* ------------------------------------------------------------------ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex(uint32_t val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--)
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xF]);
}

/* ------------------------------------------------------------------ */
/* Endian helpers                                                      */
/* ------------------------------------------------------------------ */

static inline uint32_t fdt32_to_cpu(uint32_t x)
{
    return __builtin_bswap32(x);
}

/* ------------------------------------------------------------------ */
/* Alignment                                                           */
/* ------------------------------------------------------------------ */

static inline uint32_t align4(uint32_t offset)
{
    return (offset + 3) & ~3u;
}

/* ------------------------------------------------------------------ */
/* Minimal string helpers (no libc dependency beyond memcmp/memcpy)    */
/* ------------------------------------------------------------------ */

static int jfdt_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static size_t jfdt_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

/* ------------------------------------------------------------------ */
/* Parser state (module-global, single-DTB)                            */
/* ------------------------------------------------------------------ */

static const uint8_t       *dtb_base   = NULL;
static const struct jarvis_fdt_header *hdr    = NULL;
static const uint8_t       *dt_struct  = NULL;
static const char           *dt_strings = NULL;
static uint32_t              struct_size = 0;
static bool                  jfdt_valid  = false;

/* ------------------------------------------------------------------ */
/* jarvis_fdt_init                                                     */
/* ------------------------------------------------------------------ */

int jarvis_fdt_init(const void *dtb_blob)
{
    jfdt_valid = false;

    if (!dtb_blob)
        return -1;

    dtb_base = (const uint8_t *)dtb_blob;
    hdr      = (const struct jarvis_fdt_header *)dtb_blob;

    /* Validate magic */
    if (fdt32_to_cpu(hdr->magic) != JARVIS_FDT_MAGIC) {
        debug_puts("[FDT] bad magic: 0x");
        debug_hex(fdt32_to_cpu(hdr->magic));
        debug_puts("\n");
        return -1;
    }

    /* Validate version */
    uint32_t ver = fdt32_to_cpu(hdr->version);
    if (ver < 17) {
        debug_puts("[FDT] unsupported version\n");
        return -1;
    }

    /* Store pointers to structure and strings blocks */
    dt_struct   = dtb_base + fdt32_to_cpu(hdr->off_dt_struct);
    dt_strings  = (const char *)(dtb_base + fdt32_to_cpu(hdr->off_dt_strings));
    struct_size = fdt32_to_cpu(hdr->size_dt_struct);

    jfdt_valid = true;

    debug_puts("[FDT] init OK, totalsize=0x");
    debug_hex(fdt32_to_cpu(hdr->totalsize));
    debug_puts(" struct_off=0x");
    debug_hex(fdt32_to_cpu(hdr->off_dt_struct));
    debug_puts(" strings_off=0x");
    debug_hex(fdt32_to_cpu(hdr->off_dt_strings));
    debug_puts("\n");

    return 0;
}

bool jarvis_fdt_is_valid(void)
{
    return jfdt_valid;
}

uint32_t jarvis_fdt_totalsize(void)
{
    if (!jfdt_valid)
        return 0;
    return fdt32_to_cpu(hdr->totalsize);
}

/* ------------------------------------------------------------------ */
/* Internal: find node by path, return offset into dt_struct           */
/* Returns offset of JARVIS_FDT_BEGIN_NODE token, or -1 if not found. */
/* ------------------------------------------------------------------ */

static int find_node_offset(const char *path)
{
    if (!jfdt_valid || !path)
        return -1;

    uint32_t offset = 0;
    int depth = 0;
    int target_depth = 0;

    /* Parse path: skip leading '/' */
    const char *p = path;
    if (*p == '/')
        p++;

    /* Root node request: "/" */
    if (*p == '\0') {
        if (offset < struct_size) {
            uint32_t token = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
            if (token == JARVIS_FDT_BEGIN_NODE)
                return (int)offset;
        }
        return -1;
    }

    /* Identify first path component */
    const char *component = p;
    const char *next_slash = component;
    while (*next_slash && *next_slash != '/')
        next_slash++;
    int comp_len = (int)(next_slash - component);

    /* Walk structure block tokens */
    while (offset < struct_size) {
        uint32_t token = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
        offset += 4;

        switch (token) {
        case JARVIS_FDT_BEGIN_NODE: {
            const char *name = (const char *)(dt_struct + offset);
            int name_len = (int)jfdt_strlen(name);
            offset += align4((uint32_t)name_len + 1);
            depth++;

            /* Root node has empty name "" -- auto-advance since
             * the leading "/" in the path already accounts for root */
            if (depth == 1 && name_len == 0) {
                target_depth = 1;
                break;
            }

            if (depth == target_depth + 1) {
                /* Check if this node matches the current path component */
                bool match = (name_len == comp_len) &&
                             (memcmp(name, component, (size_t)comp_len) == 0);
                if (match) {
                    target_depth++;
                    if (*next_slash == '\0') {
                        /* Final component -- return offset of BEGIN_NODE */
                        return (int)(offset - align4((uint32_t)name_len + 1) - 4);
                    }
                    /* Advance to next path component */
                    component = next_slash + 1;
                    next_slash = component;
                    while (*next_slash && *next_slash != '/')
                        next_slash++;
                    comp_len = (int)(next_slash - component);
                }
            }
            break;
        }
        case JARVIS_FDT_END_NODE:
            if (depth == target_depth)
                return -1;   /* Target child not found at this level */
            depth--;
            break;

        case JARVIS_FDT_PROP: {
            uint32_t len = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
            offset += 8;             /* skip len + nameoff */
            offset += align4(len);
            break;
        }
        case JARVIS_FDT_NOP:
            break;

        case JARVIS_FDT_END:
            return -1;

        default:
            return -1;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Skip past a node's BEGIN_NODE token + name, return new offset       */
/* ------------------------------------------------------------------ */

static uint32_t skip_node_name(uint32_t node_off)
{
    uint32_t offset = node_off;
    uint32_t token = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
    offset += 4;

    if (token == JARVIS_FDT_BEGIN_NODE) {
        const char *name = (const char *)(dt_struct + offset);
        offset += align4((uint32_t)jfdt_strlen(name) + 1);
    }
    return offset;
}

/* ------------------------------------------------------------------ */
/* jarvis_fdt_get_prop                                                 */
/* ------------------------------------------------------------------ */

struct jarvis_fdt_prop_result jarvis_fdt_get_prop(const char *path, const char *propname)
{
    struct jarvis_fdt_prop_result result = { .data = NULL, .len = 0, .found = false };

    if (!jfdt_valid || !path || !propname)
        return result;

    int node_off = find_node_offset(path);
    if (node_off < 0)
        return result;

    /* Skip past BEGIN_NODE + node name */
    uint32_t offset = skip_node_name((uint32_t)node_off);

    /* Scan properties at this node level */
    while (offset < struct_size) {
        uint32_t token = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
        offset += 4;

        if (token == JARVIS_FDT_PROP) {
            uint32_t len     = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
            uint32_t nameoff = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset + 4));
            offset += 8;

            const char *pname = dt_strings + nameoff;
            if (jfdt_strcmp(pname, propname) == 0) {
                result.data  = dt_struct + offset;
                result.len   = len;
                result.found = true;
                return result;
            }
            offset += align4(len);
        } else if (token == JARVIS_FDT_BEGIN_NODE || token == JARVIS_FDT_END_NODE || token == JARVIS_FDT_END) {
            /* Left this node's property region */
            break;
        } else if (token == JARVIS_FDT_NOP) {
            continue;
        } else {
            break;
        }
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* jarvis_fdt_get_reg                                                  */
/* Our DTS uses #address-cells=2, #size-cells=1 in /soc.              */
/* reg = <addr_hi addr_lo size>  (12 bytes minimum)                    */
/* ------------------------------------------------------------------ */

struct jarvis_fdt_reg_result jarvis_fdt_get_reg(const char *path)
{
    struct jarvis_fdt_reg_result result = { .base = 0, .size = 0, .found = false };

    struct jarvis_fdt_prop_result prop = jarvis_fdt_get_prop(path, "reg");
    if (!prop.found || prop.len < 12)
        return result;

    /* 2 cells (8 bytes) for base address */
    uint32_t addr_hi = fdt32_to_cpu(*(const uint32_t *)(prop.data));
    uint32_t addr_lo = fdt32_to_cpu(*(const uint32_t *)(prop.data + 4));
    result.base = ((uint64_t)addr_hi << 32) | addr_lo;

    /* 1 cell (4 bytes) for size */
    result.size = fdt32_to_cpu(*(const uint32_t *)(prop.data + 8));

    result.found = true;
    return result;
}

/* ------------------------------------------------------------------ */
/* jarvis_fdt_get_u32                                                  */
/* ------------------------------------------------------------------ */

uint32_t jarvis_fdt_get_u32(const char *path, const char *propname, uint32_t default_val)
{
    struct jarvis_fdt_prop_result prop = jarvis_fdt_get_prop(path, propname);
    if (!prop.found || prop.len < 4)
        return default_val;
    return fdt32_to_cpu(*(const uint32_t *)prop.data);
}

/* ------------------------------------------------------------------ */
/* jarvis_fdt_get_string                                               */
/* ------------------------------------------------------------------ */

const char *jarvis_fdt_get_string(const char *path, const char *propname)
{
    struct jarvis_fdt_prop_result prop = jarvis_fdt_get_prop(path, propname);
    if (!prop.found || prop.len == 0)
        return NULL;
    return (const char *)prop.data;
}

/* ------------------------------------------------------------------ */
/* jarvis_fdt_count_children                                           */
/* ------------------------------------------------------------------ */

int jarvis_fdt_count_children(const char *path)
{
    if (!jfdt_valid || !path)
        return 0;

    int node_off = find_node_offset(path);
    if (node_off < 0)
        return 0;

    /* Skip past this node's BEGIN_NODE + name */
    uint32_t offset = skip_node_name((uint32_t)node_off);

    int count = 0;
    int depth = 0;

    while (offset < struct_size) {
        uint32_t token = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
        offset += 4;

        switch (token) {
        case JARVIS_FDT_BEGIN_NODE: {
            if (depth == 0)
                count++;
            depth++;
            const char *n = (const char *)(dt_struct + offset);
            offset += align4((uint32_t)jfdt_strlen(n) + 1);
            break;
        }
        case JARVIS_FDT_END_NODE:
            depth--;
            if (depth < 0)
                return count;   /* Exited parent node */
            break;

        case JARVIS_FDT_PROP: {
            uint32_t len = fdt32_to_cpu(*(const uint32_t *)(dt_struct + offset));
            offset += 8 + align4(len);
            break;
        }
        case JARVIS_FDT_NOP:
            break;

        case JARVIS_FDT_END:
            return count;

        default:
            return count;
        }
    }
    return count;
}
