/*
 * JARVIS AI-OS - IPC Transport Abstraction
 * Phase 3 Pre-Work
 *
 * Provides a common interface for IPC regardless of transport:
 *   - JARVIS_IPC_UART:  Phase 2 UART serial transport
 *   - JARVIS_IPC_SHMEM: Phase 3 shared memory transport
 *
 * Usage:
 *   #define JARVIS_IPC_SHMEM   (or JARVIS_IPC_UART)
 *   #include "ipc_transport.h"
 */

#ifndef IPC_TRANSPORT_H
#define IPC_TRANSPORT_H

#include <stdint.h>

#ifdef JARVIS_IPC_UART

/* Phase 2 UART transport */
int ipc_send(uint8_t type, uint16_t seq, const void *payload, uint16_t len);
int ipc_recv(uint8_t *type, uint16_t *seq, void *payload, uint16_t *len);

#elif defined(JARVIS_IPC_SHMEM)

#include "shmem_ipc.h"

/* Phase 3 shared memory transport -- caller provides ring pointer */
#define ipc_send(ring, type, seq, payload, len) \
    shmem_ipc_send((ring), (type), (seq), (payload), (len))

#define ipc_recv(ring, type, seq, payload, len) \
    shmem_ipc_recv((ring), (type), (seq), (payload), (len))

#else
#error "Define JARVIS_IPC_UART or JARVIS_IPC_SHMEM before including ipc_transport.h"
#endif

#endif /* IPC_TRANSPORT_H */
