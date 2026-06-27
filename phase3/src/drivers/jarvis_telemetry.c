/**
 * jarvis_telemetry.c - CRC-32 + finalize for the JARVIS telemetry packet
 *
 * Self-contained (no string.h needed): a bitwise zlib/IEEE CRC-32 (must equal
 * Python zlib.crc32 so the N-c-2 receiver validates trivially) and the packet
 * finalizer. Bitwise (no table) is plenty fast for the 204-byte CRC region at ~1 Hz.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b N-c)
 */

#include "jarvis_telemetry.h"
#include <stddef.h>   /* offsetof */

uint32_t jarvis_tlm_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

void jarvis_tlm_finalize(telemetry_packet_t *pkt)
{
    pkt->magic   = JARVIS_TLM_MAGIC;
    pkt->version = JARVIS_TLM_VERSION;
    pkt->crc32   = jarvis_tlm_crc32(pkt, (uint32_t)offsetof(telemetry_packet_t, crc32));
}
