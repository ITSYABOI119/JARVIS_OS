/* JARVIS AI-OS — HOST-ONLY fake <sel4/sel4.h> for unit-testing the deployed
 * seL4-native work-stealing pool (phase3/src/ai/threadpool_sel4.c, M3) OFF-BOX.
 *
 * This is NOT a real seL4 header and is NEVER used in any deployed build. It exists
 * only so threadpool_sel4.c (and threadpool.h's JARVIS_SEL4_SMP block) compile and
 * RUN on a normal host with ZERO edits to the production file, under ThreadSanitizer.
 *
 * It implements ONLY the seL4 surface threadpool_sel4.c actually uses (verified by
 * reading the file): types seL4_CPtr / seL4_Word / seL4_IPCBuffer, and functions
 * seL4_Wait(cptr, sender) / seL4_Signal(cptr) / seL4_SetIPCBuffer(buf).
 *
 * Model: a seL4_CPtr carries the address of a host POSIX sem_t. The production code
 * only ever passes cptrs to seL4_Wait/seL4_Signal (it never dereferences them), so an
 * opaque integer handle is a faithful stand-in for a real capability. seL4 Notification
 * signal/wait map to sem_post/sem_wait — both "remember" a signal that precedes a wait,
 * which is exactly the per-worker-wake reuse the pool relies on across dispatches.
 */
#ifndef JARVIS_TEST_FAKE_SEL4_H
#define JARVIS_TEST_FAKE_SEL4_H

/* <semaphore.h> hides its POSIX declarations under a strict -std=c11 dialect; expose
 * them. The CI command also passes -D_POSIX_C_SOURCE so this is set before the FIRST
 * system header in every TU (a guard here alone can be too late after <stddef.h>). */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stddef.h>
#include <semaphore.h>

typedef uintptr_t seL4_CPtr;
typedef unsigned long seL4_Word;
typedef struct { int reserved_; } seL4_IPCBuffer;

/* A cptr holds the address of a host sem_t (see header comment). */
static inline sem_t *jarvis_stub_sem(seL4_CPtr c) {
    return (sem_t *)(uintptr_t)c;
}

/* seL4_Wait(src, sender) — block until the Notification is signalled. The production
 * code passes sender = NULL and ignores any return, so a void shim is faithful. */
static inline void seL4_Wait(seL4_CPtr src, seL4_Word *sender) {
    (void)sender;
    while (sem_wait(jarvis_stub_sem(src)) != 0) {
        /* retry on EINTR (the only error possible for a valid, process-shared sem) */
    }
}

/* seL4_Signal(dst) — signal the Notification (wake one waiter / remember the signal). */
static inline void seL4_Signal(seL4_CPtr dst) {
    int r = sem_post(jarvis_stub_sem(dst));
    (void)r;
}

/* seL4_SetIPCBuffer(buf) — on the box this registers the worker's IPC-buffer TLS before
 * its first syscall; on the host there is no IPC buffer, so it is a no-op. */
static inline void seL4_SetIPCBuffer(seL4_IPCBuffer *buf) {
    (void)buf;
}

#endif /* JARVIS_TEST_FAKE_SEL4_H */
