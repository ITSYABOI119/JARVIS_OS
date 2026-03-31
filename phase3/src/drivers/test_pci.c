/**
 * test_pci.c - PCI Bus Enumeration Driver Tests
 *
 * Mock I/O port access to simulate 3 PCI devices on bus 0:
 *   Dev 0: Host bridge     (class 0x06, subclass 0x00)
 *   Dev 1: AHCI controller (class 0x01, subclass 0x06, vendor 0x8086)
 *   Dev 2: Ethernet NIC    (class 0x02, subclass 0x00, vendor 0x8086)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers test_pci.c pci.c -o test_pci
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pci.h"

/* ========================================================================
 * Mock I/O Port Layer
 * ======================================================================== */

static uint32_t last_cfg_addr = 0;
static int pass_count = 0;
static int fail_count = 0;

/*
 * Simulated 64-byte config space per device (16 x 32-bit registers).
 *
 * Layout per PCI spec (dword index):
 *   [0]  vendor_id | device_id
 *   [1]  command   | status
 *   [2]  revision  | prog_if | subclass | class_code
 *   [3]  cache_ln  | latency | header_type | BIST
 *   [4]  BAR0
 *   [5]  BAR1
 *   [6]  BAR2
 *   [7]  BAR3
 *   [8]  BAR4
 *   [9]  BAR5
 *   [10] CardBus CIS
 *   [11] subsys_vendor | subsys_id
 *   [12] ROM base
 *   [13] cap_ptr | reserved
 *   [14] reserved
 *   [15] int_line | int_pin | min_grant | max_latency
 */

/* Dev 0: Host bridge — vendor 0x8086, device 0x1234, class 0x06/0x00 */
static uint32_t dev0_cfg[16];

/* Dev 1: AHCI controller — vendor 0x8086, device 0x2922, class 0x01/0x06, prog_if 0x01 */
static uint32_t dev1_cfg[16];

/* Dev 2: Ethernet NIC — vendor 0x8086, device 0x100E, class 0x02/0x00 */
static uint32_t dev2_cfg[16];

/* Dev 3: GPU — vendor 0x10DE (NVIDIA), device 0x2544, class 0x03/0x00, 64-bit BAR0 */
static uint32_t dev3_cfg[16];

static void setup_mock_devices(void)
{
    memset(dev0_cfg, 0, sizeof(dev0_cfg));
    memset(dev1_cfg, 0, sizeof(dev1_cfg));
    memset(dev2_cfg, 0, sizeof(dev2_cfg));
    memset(dev3_cfg, 0, sizeof(dev3_cfg));

    /* Dev 0: Host bridge */
    dev0_cfg[0]  = 0x12348086;           /* vendor=0x8086, device=0x1234 */
    dev0_cfg[1]  = 0x00000006;           /* command=0x0006 (IO+Mem), status=0 */
    dev0_cfg[2]  = 0x06000001;           /* class=0x06, sub=0x00, prog=0x00, rev=0x01 */
    dev0_cfg[3]  = 0x00000000;           /* header_type=0x00 (single function) */
    dev0_cfg[4]  = 0x00000000;           /* BAR0: none */
    dev0_cfg[15] = 0x00000000;           /* no interrupt */

    /* Dev 1: AHCI controller */
    dev1_cfg[0]  = 0x29228086;           /* vendor=0x8086, device=0x2922 */
    dev1_cfg[1]  = 0x00000007;           /* command=0x0007 (IO+Mem+BusMaster) */
    dev1_cfg[2]  = 0x01060100;           /* class=0x01, sub=0x06, prog=0x01, rev=0x00 */
    dev1_cfg[3]  = 0x00000000;           /* header_type=0x00 */
    dev1_cfg[4]  = 0xFEBFF000;           /* BAR0: MMIO at 0xFEBFF000 */
    dev1_cfg[5]  = 0x0000E001;           /* BAR1: I/O at 0xE000 */
    dev1_cfg[15] = 0x00010B00;           /* int_line=0, int_pin=INTA, ... */

    /* Dev 2: Ethernet NIC */
    dev2_cfg[0]  = 0x100E8086;           /* vendor=0x8086, device=0x100E (e1000) */
    dev2_cfg[1]  = 0x00000003;           /* command=0x0003 (IO+Mem) */
    dev2_cfg[2]  = 0x02000002;           /* class=0x02, sub=0x00, prog=0x00, rev=0x02 */
    dev2_cfg[3]  = 0x00000000;           /* header_type=0x00 */
    dev2_cfg[4]  = 0xFEBC0000;           /* BAR0: MMIO at 0xFEBC0000 */
    dev2_cfg[5]  = 0x0000C001;           /* BAR1: I/O at 0xC000 */
    dev2_cfg[15] = 0x00010A00;           /* int_line=0, int_pin=INTA, ... */

    /* Dev 3: GPU with 64-bit BAR (simulates RTX 3060) */
    dev3_cfg[0]  = 0x254410DE;           /* vendor=0x10DE (NVIDIA), device=0x2544 */
    dev3_cfg[1]  = 0x00000007;           /* command=0x0007 (IO+Mem+BusMaster) */
    dev3_cfg[2]  = 0x03000001;           /* class=0x03, sub=0x00, prog=0x00, rev=0x01 */
    dev3_cfg[3]  = 0x00000000;           /* header_type=0x00 */
    dev3_cfg[4]  = 0xFC000004;           /* BAR0: 64-bit MMIO (type=0x04), low bits addr=0xFC000000 */
    dev3_cfg[5]  = 0x00000001;           /* BAR1: upper 32 bits of BAR0 = 0x00000001 -> full addr = 0x1_FC000000 */
    dev3_cfg[6]  = 0xDE000000;           /* BAR2: 32-bit MMIO at 0xDE000000 */
    dev3_cfg[15] = 0x00010B00;           /* int_line=0, int_pin=INTA */
}

/* --- Mock outl/inl --- */

void outl(uint16_t port, uint32_t val)
{
    if (port == PCI_CONFIG_ADDRESS) {
        last_cfg_addr = val;
    } else if (port == PCI_CONFIG_DATA) {
        /* Config write: decode address and update simulated device */
        uint8_t bus  = (uint8_t)((last_cfg_addr >> 16) & 0xFF);
        uint8_t dev  = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
        uint8_t func = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
        uint8_t off  = (uint8_t)(last_cfg_addr & 0xFC);

        if (bus != 0 || func != 0) return;

        uint32_t *cfg = NULL;
        if (dev == 0)      cfg = dev0_cfg;
        else if (dev == 1) cfg = dev1_cfg;
        else if (dev == 2) cfg = dev2_cfg;
        else if (dev == 3) cfg = dev3_cfg;
        else return;

        cfg[off / 4] = val;
    }
}

uint32_t inl(uint16_t port)
{
    if (port != PCI_CONFIG_DATA)
        return 0xFFFFFFFF;

    uint8_t bus  = (uint8_t)((last_cfg_addr >> 16) & 0xFF);
    uint8_t dev  = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
    uint8_t func = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
    uint8_t off  = (uint8_t)(last_cfg_addr & 0xFC);

    if (bus != 0 || func != 0)
        return 0xFFFFFFFF;

    uint32_t *cfg = NULL;
    if (dev == 0)      cfg = dev0_cfg;
    else if (dev == 1) cfg = dev1_cfg;
    else if (dev == 2) cfg = dev2_cfg;
    else if (dev == 3) cfg = dev3_cfg;
    else               return 0xFFFFFFFF;

    return cfg[off / 4];
}

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

#define TEST(name) \
    do { printf("  TEST: %-50s ", name); } while(0)

#define PASS() \
    do { printf("PASS\n"); pass_count++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL - %s\n", msg); fail_count++; } while(0)

#define CHECK(cond, msg) \
    do { if (cond) { PASS(); } else { FAIL(msg); } } while(0)

/* ========================================================================
 * Tests
 * ======================================================================== */

static void test_config_read_address_format(void)
{
    TEST("Config read address format");

    /* Read vendor_id of bus=0, dev=1, func=0, offset=0x00 */
    uint32_t val = pci_config_read(0, 1, 0, 0x00);

    /* Verify the address written to 0xCF8 */
    uint32_t expected_addr = (1U << 31)    /* enable */
                           | (0U  << 16)   /* bus 0 */
                           | (1U  << 11)   /* dev 1 */
                           | (0U  << 8)    /* func 0 */
                           | (0x00);       /* offset 0 */

    int addr_ok = (last_cfg_addr == expected_addr);
    int val_ok  = (val == 0x29228086);  /* AHCI dev vendor|device */
    CHECK(addr_ok && val_ok, "address or value mismatch");
}

static void test_pci_scan_finds_3_devices(void)
{
    TEST("PCI scan finds exactly 4 devices");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    CHECK(count == 4, "expected 4 devices");
}

static void test_pci_scan_device_fields(void)
{
    TEST("PCI scan populates device fields correctly");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    int ok = (count >= 3);
    /* Dev 0: host bridge */
    ok = ok && (devices[0].vendor_id  == 0x8086);
    ok = ok && (devices[0].device_id  == 0x1234);
    ok = ok && (devices[0].class_code == 0x06);
    ok = ok && (devices[0].subclass   == 0x00);
    ok = ok && (devices[0].bus == 0 && devices[0].device == 0);

    /* Dev 1: AHCI */
    ok = ok && (devices[1].vendor_id  == 0x8086);
    ok = ok && (devices[1].device_id  == 0x2922);
    ok = ok && (devices[1].class_code == 0x01);
    ok = ok && (devices[1].subclass   == 0x06);
    ok = ok && (devices[1].prog_if    == 0x01);

    /* Dev 2: NIC */
    ok = ok && (devices[2].vendor_id  == 0x8086);
    ok = ok && (devices[2].device_id  == 0x100E);
    ok = ok && (devices[2].class_code == 0x02);
    ok = ok && (devices[2].subclass   == 0x00);

    CHECK(ok, "device fields mismatch");
}

static void test_find_ahci(void)
{
    TEST("Find AHCI by class 0x01 subclass 0x06");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *ahci = pci_find_device(PCI_CLASS_STORAGE,
                                          PCI_SUBCLASS_SATA,
                                          devices, count);
    int ok = (ahci != NULL);
    ok = ok && (ahci->vendor_id == 0x8086);
    ok = ok && (ahci->device_id == 0x2922);
    ok = ok && (ahci->prog_if   == PCI_PROG_IF_AHCI);
    CHECK(ok, "AHCI not found or fields wrong");
}

static void test_find_nic(void)
{
    TEST("Find NIC by class 0x02 subclass 0x00");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *nic = pci_find_device(PCI_CLASS_NETWORK,
                                         PCI_SUBCLASS_ETHERNET,
                                         devices, count);
    int ok = (nic != NULL);
    ok = ok && (nic->vendor_id == 0x8086);
    ok = ok && (nic->device_id == 0x100E);
    CHECK(ok, "NIC not found or fields wrong");
}

static void test_find_nonexistent(void)
{
    TEST("Find nonexistent class returns NULL");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    /* class 0xFF/0xFF should not exist */
    pci_device_t *none = pci_find_device(0xFF, 0xFF, devices, count);
    CHECK(none == NULL, "expected NULL for missing class");
}

static void test_bar_mmio_address(void)
{
    TEST("BAR MMIO address extraction");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    pci_scan(devices, 32);

    /* AHCI BAR0 = 0xFEBFF000 (MMIO, low 4 bits should be 0) */
    uint64_t addr = pci_get_bar_address(&devices[1], 0);
    CHECK(addr == 0xFEBFF000ULL, "AHCI BAR0 address wrong");
}

static void test_bar_io_address(void)
{
    TEST("BAR I/O port address extraction");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    pci_scan(devices, 32);

    /* AHCI BAR1 = 0x0000E001 -> I/O at 0xE000 (mask low 2 bits) */
    uint64_t addr = pci_get_bar_address(&devices[1], 1);
    CHECK(addr == 0x0000E000ULL, "AHCI BAR1 I/O address wrong");
}

static void test_bar_type_detection(void)
{
    TEST("BAR type detection (MMIO vs I/O)");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    pci_scan(devices, 32);

    /* AHCI BAR0 = MMIO, BAR1 = I/O */
    int bar0_mmio = pci_is_bar_mmio(&devices[1], 0);
    int bar1_mmio = pci_is_bar_mmio(&devices[1], 1);

    CHECK(bar0_mmio && !bar1_mmio, "BAR type detection wrong");
}

static void test_bus_master_enable(void)
{
    TEST("Bus master enable sets command bit 2");

    setup_mock_devices();  /* Reset state */

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    pci_scan(devices, 32);

    /* NIC command register starts at 0x0003 (IO+Mem, no bus master) */
    /* Enable bus master */
    pci_enable_bus_master(&devices[2]);

    /* Re-read command register from mock config space */
    uint32_t cmd = pci_config_read(0, 2, 0, PCI_COMMAND);
    int bus_master_set = (cmd & PCI_CMD_BUS_MASTER) != 0;
    CHECK(bus_master_set, "bus master bit not set after enable");
}

static void test_missing_device_skipped(void)
{
    TEST("Missing device (vendor 0xFFFF) skipped");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    /* Verify no device has vendor 0xFFFF */
    int ok = 1;
    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == 0xFFFF) {
            ok = 0;
            break;
        }
    }
    CHECK(ok, "device with vendor 0xFFFF found in results");
}

/* ---- 64-bit BAR tests ---- */

static void test_bar_64bit_address(void)
{
    TEST("64-bit BAR combines BAR[n] and BAR[n+1]");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *gpu = pci_find_device(PCI_CLASS_DISPLAY, PCI_SUBCLASS_VGA,
                                         devices, count);
    int ok = (gpu != NULL);
    if (ok) {
        uint64_t bar0 = pci_get_bar_address(gpu, 0);
        ok = (bar0 == 0x00000001FC000000ULL);
    }
    CHECK(ok, "64-bit BAR address mismatch");
}

static void test_bar_64bit_detect(void)
{
    TEST("pci_is_bar_64bit detects 64-bit type");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *gpu = pci_find_device(PCI_CLASS_DISPLAY, PCI_SUBCLASS_VGA,
                                         devices, count);
    int ok = (gpu != NULL);
    ok = ok && pci_is_bar_64bit(gpu, 0);   /* BAR0 is 64-bit */
    ok = ok && !pci_is_bar_64bit(gpu, 2);  /* BAR2 is 32-bit */
    CHECK(ok, "64-bit detection wrong");
}

static void test_bar_64bit_upper_bits(void)
{
    TEST("BAR[n+1] contains upper 32 bits for 64-bit BAR");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *gpu = pci_find_device(PCI_CLASS_DISPLAY, PCI_SUBCLASS_VGA,
                                         devices, count);
    int ok = (gpu != NULL);
    ok = ok && (gpu->bar[1] == 0x00000001);
    CHECK(ok, "upper 32 bits wrong");
}

static void test_bar_32bit_on_64bit_device(void)
{
    TEST("32-bit BAR on 64-bit capable device");

    pci_device_t devices[32];
    memset(devices, 0, sizeof(devices));
    int count = pci_scan(devices, 32);

    pci_device_t *gpu = pci_find_device(PCI_CLASS_DISPLAY, PCI_SUBCLASS_VGA,
                                         devices, count);
    int ok = (gpu != NULL);
    if (ok) {
        uint64_t bar2 = pci_get_bar_address(gpu, 2);
        ok = (bar2 == 0xDE000000ULL);
    }
    CHECK(ok, "32-bit BAR on 64-bit device wrong");
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== PCI Bus Enumeration Driver Tests ===\n\n");

    setup_mock_devices();

    test_config_read_address_format();
    test_pci_scan_finds_3_devices();
    test_pci_scan_device_fields();
    test_find_ahci();
    test_find_nic();
    test_find_nonexistent();
    test_bar_mmio_address();
    test_bar_io_address();
    test_bar_type_detection();
    test_bus_master_enable();
    test_missing_device_skipped();
    test_bar_64bit_address();
    test_bar_64bit_detect();
    test_bar_64bit_upper_bits();
    test_bar_32bit_on_64bit_device();

    printf("\n=== Results: %d PASS, %d FAIL (of %d) ===\n",
           pass_count, fail_count, pass_count + fail_count);

    return fail_count > 0 ? 1 : 0;
}
