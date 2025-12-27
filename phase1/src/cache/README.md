# Decision Cache Component

The Decision Cache is a high-performance hash table that stores pre-compiled AI decision patterns for common operations, providing 135x speedup (50ms AI → <1ms cache lookup).

## Overview

Instead of invoking the AI model for every command, the cache stores normalized query patterns mapped to pre-computed responses. This enables sub-millisecond response times for ~85% of common operations.

## Files

| File | Description |
|------|-------------|
| `decision_cache.c` | Core hash table implementation |
| `decision_cache.h` | API declarations and constants |
| `cache_patterns.c` | 258 pre-compiled patterns |
| `test_cache.c` | Unit tests (12 tests) |

## Key Features

- **FNV-1a Hash**: 64-bit hash function for uniform distribution
- **512 Buckets**: Hash table size (increased from 256 in Week 25)
- **258 Patterns**: Pre-compiled responses for common commands
- **Query Normalization**: Lowercase, whitespace collapse, trim
- **O(1) Lookup**: Average constant-time retrieval

## API

```c
#include "decision_cache.h"

// Initialize cache with pre-compiled patterns
decision_cache_t* cache = cache_init();

// Lookup a query
cache_result_t result;
if (cache_lookup(cache, "list files", &result)) {
    printf("Hit: action=%d, trust=%d\n", result.action, result.trust_level);
}

// Add custom pattern
cache_add_pattern(cache, "show disk usage", "ACTION:show_disk|TRUST:0|HIT:1");

// Get statistics
cache_stats_t stats = cache_get_stats(cache);
printf("Hit rate: %.1f%%\n", 100.0 * stats.hits / stats.lookups);

// Cleanup
cache_destroy(cache);
```

## Query Normalization

All queries are normalized before hashing:

1. Convert to lowercase
2. Collapse multiple spaces to single space
3. Trim leading/trailing whitespace

Example: `"  LIST   Files  "` → `"list files"`

## Pattern Categories

The 258 pre-compiled patterns cover:

| Category | Count | Examples |
|----------|-------|----------|
| File Operations | 45 | ls, cat, mkdir, rm |
| Process Mgmt | 30 | ps, top, kill |
| System Info | 35 | status, uptime, battery |
| Network | 25 | ping, ifconfig, netstat |
| Help/General | 40 | help, exit, clear |
| AI Queries | 50 | explain, describe, how to |
| SHIELD | 33 | dangerous operations |

## Performance

- **Hit Rate**: 85.7% in QEMU testing (target: >80%)
- **Lookup Time**: <0.1ms (validated)
- **Memory**: ~150KB for 512 entries + patterns

## Building

```bash
cd phase1/src/cache
gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm
./test_cache
```

## Integration

The cache is used by:

1. **seL4 Kernel** (`phase1/src/sel4/main.c`): Handles IPC cache lookups
2. **Python IPC Client** (`phase1/src/ai/ipc_client.py`): Sends queries to cache
3. **Shell** (`phase1/src/shell/shell.py`): Routes commands through cache

## Configuration

Key constants in `decision_cache.h`:

```c
#define CACHE_SIZE          512    // Hash table buckets
#define MAX_QUERY_LENGTH    256    // Max query string length
#define MAX_RESPONSE_LENGTH 512    // Max response string length
#define CACHE_HIT_INDICATOR  1     // Response indicates cache hit
```

## Related Documentation

- `phase1/PHASE_1_ARCHITECTURE.md` - System design
- `phase1/docs/PHASE_1_FINAL_REPORT.md` - Performance results
- `phase0/experiments/decision_cache_benchmark.py` - Initial validation
