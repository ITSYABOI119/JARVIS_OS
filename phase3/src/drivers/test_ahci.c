/**
 * test_ahci.c - AHCI Controller Discovery Unit Tests
 *
 * Mock MMIO tests for AHCI HBA discovery/probe.
 * Simulates a 2-port controller with one SATA device on port 0.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers test_ahci.c ahci.c -o test_ahci
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ahci.h"

/* ========================================================================
 * Mock MMIO Layer
 *
 * Simulated HBA memory region (4 KB).
 * ahci.c passes (mmio_base + offset) as the address; we set mmio_base=0
 * so the address IS the offset directly.
 * ======================================================================== */

static uint8_t mock_hba[4096] = {0};

uint32_t mmio_read32(volatile void *addr)
{
    uintptr_t offset = (uintptr_t)addr;
    if (offset + 4 <= sizeof(mock_hba))
        return *(uint32_t *)(mock_hba + offset);
    return 0;
}

void mmio_write32(volatile void *addr, uint32_t val)
{
    uintptr_t offset = (uintptr_t)addr;
    if (offset + 4 <= sizeof(mock_hba))
        *(uint32_t *)(mock_hba + offset) = val;
}

/* ========================================================================
 * Simulated Controller Configuration
 *
 * - 2 ports implemented (ports 0 and 1): PI = 0x03
 * - Port 0: SATA disk present (DET=3, IPM=1), signature 0x00000101
 * - Port 1: No device (DET=0, IPM=0)
 * - Version 1.3.1 (0x00010301)
 * - 32 command slots: NCS bits 12:8 = 31 (0-based)
 * - 64-bit capable: CAP bit 31 = 1
 * - NP bits 4:0 = 1 (0-based, meaning 2 ports)
 * ======================================================================== */

static void mock_setup(void)
{
    uint32_t cap;

    memset(mock_hba, 0, sizeof(mock_hba));

    /* CAP register (offset 0x00):
     *   bits 4:0  = 1 (NP: 2 ports, 0-based)
     *   bits 12:8 = 31 (NCS: 32 command slots, 0-based)
     *   bit 31    = 1 (S64A: 64-bit support)
     */
    cap = 1                      /* NP = 1 (2 ports) */
        | (31U << CAP_NCS_SHIFT) /* NCS = 31 (32 slots) */
        | CAP_S64A;              /* 64-bit capable */
    *(uint32_t *)(mock_hba + HBA_REG_CAP) = cap;

    /* GHC register (offset 0x04): starts with AE clear */
    *(uint32_t *)(mock_hba + HBA_REG_GHC) = 0;

    /* PI register (offset 0x0C): ports 0 and 1 implemented */
    *(uint32_t *)(mock_hba + HBA_REG_PI) = 0x03;

    /* VS register (offset 0x10): version 1.3.1 = 0x00010301 */
    *(uint32_t *)(mock_hba + HBA_REG_VS) = 0x00010301;

    /* Port 0 registers: base = 0x100 + 0*0x80 = 0x100
     *   SSTS (0x100 + 0x28 = 0x128): DET=3, IPM=1 => 0x00000103
     *   SIG  (0x100 + 0x24 = 0x124): SATA disk => 0x00000101
     */
    *(uint32_t *)(mock_hba + 0x128) = PORT_SSTS_DET_ACTIVE | PORT_SSTS_IPM_ACTIVE;
    *(uint32_t *)(mock_hba + 0x124) = SATA_SIG_ATA;

    /* Port 1 registers: base = 0x100 + 1*0x80 = 0x180
     *   SSTS (0x180 + 0x28 = 0x1A8): DET=0, IPM=0 => 0x00000000
     *   SIG  (0x180 + 0x24 = 0x1A4): 0x00000000
     */
    *(uint32_t *)(mock_hba + 0x1A8) = 0x00000000;
    *(uint32_t *)(mock_hba + 0x1A4) = 0x00000000;
}

/* ========================================================================
 * ahci_controller_t is defined in ahci.c (not exported in ahci.h).
 * Re-declare the struct and discovery API here for testing.
 * ======================================================================== */

typedef struct {
    uint64_t mmio_base;
    uint32_t cap;
    uint32_t version;
    uint32_t ports_impl;
    uint32_t n_ports;
    uint32_t n_cmd_slots;
    int      supports_64bit;
    struct {
        int      present;
        uint32_t sig;
        uint32_t ssts;
    } ports[32];
} ahci_controller_t;

extern int  ahci_discover_init(ahci_controller_t *ctrl, uint64_t mmio_base);
extern void ahci_probe_ports(ahci_controller_t *ctrl);
extern int  ahci_port_check_active(ahci_controller_t *ctrl, int port);
extern void ahci_print_info(ahci_controller_t *ctrl);

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

/* ========================================================================
 * Tests
 * ======================================================================== */

static void test_init_cap_parsing(void)
{
    ahci_controller_t ctrl;
    TEST("Init -- CAP parsing (ports, cmd slots, 64-bit)");
    mock_setup();

    int rc = ahci_discover_init(&ctrl, 0);
    if (rc != 0) FAIL("ahci_discover_init returned non-zero");

    if (ctrl.n_ports != 2) FAIL("expected 2 ports");
    if (ctrl.n_cmd_slots != 32) FAIL("expected 32 command slots");
    if (!ctrl.supports_64bit) FAIL("expected 64-bit support");

    /* Verify GHC.AE was set */
    uint32_t ghc = *(uint32_t *)(mock_hba + HBA_REG_GHC);
    if (!(ghc & GHC_AE)) FAIL("GHC.AE not set after init");

    PASS();
}

static void test_version_parsing(void)
{
    ahci_controller_t ctrl;
    TEST("Version parsing (1.3.1)");
    mock_setup();

    ahci_discover_init(&ctrl, 0);

    if (ctrl.version != 0x00010301) FAIL("version mismatch");

    /* Verify major.minor decode */
    uint32_t major = (ctrl.version >> 16) & 0xFFFF;
    uint32_t minor_hi = (ctrl.version >> 8) & 0xFF;
    uint32_t minor_lo = ctrl.version & 0xFF;
    if (major != 1)    FAIL("major != 1");
    if (minor_hi != 3) FAIL("minor_hi != 3");
    if (minor_lo != 1) FAIL("minor_lo != 1");

    PASS();
}

static void test_port_probe(void)
{
    ahci_controller_t ctrl;
    TEST("Port probe -- port 0 SATA, port 1 empty");
    mock_setup();

    ahci_discover_init(&ctrl, 0);
    ahci_probe_ports(&ctrl);

    /* Port 0: should be present with SATA signature */
    if (!ctrl.ports[0].present) FAIL("port 0 not detected as present");
    if (ctrl.ports[0].sig != SATA_SIG_ATA) FAIL("port 0 sig != SATA_SIG_ATA");

    /* Port 1: should not be present */
    if (ctrl.ports[1].present) FAIL("port 1 incorrectly detected as present");

    PASS();
}

static void test_port_active(void)
{
    ahci_controller_t ctrl;
    TEST("Port active check -- port 0 active, port 1 not");
    mock_setup();

    ahci_discover_init(&ctrl, 0);
    ahci_probe_ports(&ctrl);

    if (!ahci_port_check_active(&ctrl, 0)) FAIL("port 0 should be active");
    if (ahci_port_check_active(&ctrl, 1))  FAIL("port 1 should not be active");

    /* Out-of-range port should return 0 */
    if (ahci_port_check_active(&ctrl, -1)) FAIL("port -1 should return 0");
    if (ahci_port_check_active(&ctrl, 32)) FAIL("port 32 should return 0");

    PASS();
}

static void test_print_info(void)
{
    ahci_controller_t ctrl;
    TEST("Print info -- no crash");
    mock_setup();

    ahci_discover_init(&ctrl, 0);
    ahci_probe_ports(&ctrl);

    printf("\n--- ahci_print_info output ---\n");
    ahci_print_info(&ctrl);
    printf("--- end ---\n");

    /* If we got here without crashing, pass */
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== JARVIS AI-OS: AHCI Discovery Tests ===\n\n");

    test_init_cap_parsing();
    test_version_parsing();
    test_port_probe();
    test_port_active();
    test_print_info();

    printf("\n=== Results: %d/%d PASSED ===\n", tests_passed, tests_run);

    if (tests_passed == tests_run)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
