/*
 * JARVIS AI-OS - seL4-compatible Atomics
 * Week 4: Wrapper for atomic operations without stdatomic.h
 *
 * seL4 builds with -nostdinc, so we can't use <stdatomic.h>.
 * This provides equivalent functionality using GCC built-ins.
 */

#ifndef SEL4_ATOMICS_H
#define SEL4_ATOMICS_H

#include <stdint.h>

/*
 * Atomic types (using volatile for seL4 compatibility)
 */
typedef struct {
    volatile uint64_t value;
} atomic_uint_fast64_t;

/*
 * Memory ordering (map to GCC __ATOMIC_* constants)
 */
typedef enum {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

/*
 * Atomic operations using GCC built-ins
 */

static inline void atomic_init(atomic_uint_fast64_t *obj, uint64_t value)
{
    obj->value = value;
}

static inline uint64_t atomic_load_explicit(const atomic_uint_fast64_t *obj, memory_order order)
{
    return __atomic_load_n(&obj->value, order);
}

static inline void atomic_store_explicit(atomic_uint_fast64_t *obj, uint64_t value, memory_order order)
{
    __atomic_store_n(&obj->value, value, order);
}

static inline uint64_t atomic_fetch_add_explicit(atomic_uint_fast64_t *obj, uint64_t arg, memory_order order)
{
    return __atomic_fetch_add(&obj->value, arg, order);
}

#endif /* SEL4_ATOMICS_H */
