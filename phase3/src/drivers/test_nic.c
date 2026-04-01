/**
 * test_nic.c - Realtek RTL8168/8111 NIC Driver Unit Tests
 *
 * Mock MMIO and PCI tests for the RTL8168 NIC driver.
 * Simulates a Realtek 10EC:8168 device on PCI bus 0 / device 3.
 *
 * Tests:
 *   1. PCI probe - find RTL8168 via vendor:device ID
 *   2. MAC address read - verify 6-byte extraction from IDR0/IDR4
 *   3. TX descriptor build - verify OWN, FS, LS, length, EOR bits
 *   4. RX ring wrap - verify index wrapping at 256
 *   5. Link status - mock PHY status for up/down/speed detection
 *   6. Send basic - verify TX descriptor and polling kick
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers phase3/src/drivers/test_nic.c \
 *       phase3/src/drivers/nic_rtl8168.c phase3/src/drivers/pci.c \
 *       -o /tmp/test_nic && /tmp/test_nic
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nic_rtl8168.h"
#include "pci.h"

/* ========================================================================
 * Mock MMIO Layer
 *
 * Simulated NIC MMIO region (512 bytes covers all registers up to 0xEC+).
 * nic_rtl8168.c computes (mmio_base + offset); we set mmio_base=0x10000
 * and use (addr - 0x10000) as the index into mock_mmio[].
 * ======================================================================== */

#define MOCK_MMIO_BASE  0x10000
#define MOCK_MMIO_SIZE  512

static uint8_t mock_mmio[MOCK_MMIO_SIZE];

uint8_t mmio_read8(volatile void *addr)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset < MOCK_MMIO_SIZE) {
        uint8_t val = mock_mmio[offset];
        /* Simulate hardware auto-clearing RST bit in Command Register */
        if (offset == RTL_CR && (val & RTL_CR_RST))
            mock_mmio[offset] &= ~RTL_CR_RST;
        return val;
    }
    return 0;
}

uint16_t mmio_read16(volatile void *addr)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset + 2 <= MOCK_MMIO_SIZE) {
        uint16_t val;
        memcpy(&val, &mock_mmio[offset], sizeof(val));
        return val;
    }
    return 0;
}

uint32_t mmio_read32(volatile void *addr)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset + 4 <= MOCK_MMIO_SIZE) {
        uint32_t val;
        memcpy(&val, &mock_mmio[offset], sizeof(val));
        return val;
    }
    return 0;
}

void mmio_write8(volatile void *addr, uint8_t val)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset < MOCK_MMIO_SIZE)
        mock_mmio[offset] = val;
}

void mmio_write16(volatile void *addr, uint16_t val)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset + 2 <= MOCK_MMIO_SIZE)
        memcpy(&mock_mmio[offset], &val, sizeof(val));
}

void mmio_write32(volatile void *addr, uint32_t val)
{
    uintptr_t offset = (uintptr_t)addr - MOCK_MMIO_BASE;
    if (offset + 4 <= MOCK_MMIO_SIZE)
        memcpy(&mock_mmio[offset], &val, sizeof(val));
}

/* ========================================================================
 * Mock PCI I/O Port Layer
 *
 * Simulate PCI config space for 32 device slots on bus 0.
 * Only device 3 has a Realtek RTL8168. All others return 0xFFFF vendor.
 *
 * PCI config space layout (16 x 32-bit registers per device):
 *   [0]  vendor_id | device_id
 *   [1]  command   | status
 *   [2]  revision  | prog_if | subclass | class_code
 *   [3]  cache_ln  | latency | header_type | BIST
 *   [4]  BAR0      (I/O port)
 *   [5]  BAR1
 *   [6]  BAR2      (MMIO base)
 *   ...
 *   [15] int_line  | int_pin | min_grant | max_latency
 * ======================================================================== */

/* Only device 3, function 0 has the RTL8168 */
static uint32_t rtl_pci_cfg[16];

static uint32_t last_cfg_addr = 0;

static void setup_mock_pci(void)
{
    memset(rtl_pci_cfg, 0, sizeof(rtl_pci_cfg));

    /* Device 3: Realtek RTL8168 GbE */
    rtl_pci_cfg[0]  = (RTL_DEVICE_ID_8168 << 16) | RTL_VENDOR_ID;  /* 10EC:8168 */
    rtl_pci_cfg[1]  = 0x00000003;  /* command: IO + Mem enabled */
    rtl_pci_cfg[2]  = 0x02000010;  /* class=0x02(network), sub=0x00(ethernet), prog=0x00, rev=0x10 */
    rtl_pci_cfg[3]  = 0x00000000;  /* header_type=0x00 (single function) */
    rtl_pci_cfg[4]  = 0x0000D001;  /* BAR0: I/O port at 0xD000 (bit0=1 for I/O) */
    rtl_pci_cfg[5]  = 0x00000000;  /* BAR1: unused */
    rtl_pci_cfg[6]  = MOCK_MMIO_BASE;  /* BAR2: MMIO at 0x10000 (bit0=0 for Mem) */
    rtl_pci_cfg[7]  = 0x00000000;  /* BAR3 */
    rtl_pci_cfg[8]  = 0x00000000;  /* BAR4 */
    rtl_pci_cfg[9]  = 0x00000000;  /* BAR5 */
    rtl_pci_cfg[15] = 0x00000B01;  /* int_line=11(0x0B), int_pin=1(INTA) */
}

/* PCI I/O port mock: outl/inl used by pci.c */

void outl(uint16_t port, uint32_t val)
{
    if (port == PCI_CONFIG_ADDRESS)
        last_cfg_addr = val;
    else if (port == PCI_CONFIG_DATA) {
        /* Handle writes to config space (e.g., bus master enable) */
        if (!(last_cfg_addr & PCI_ADDR_ENABLE))
            return;

        uint8_t dev    = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
        uint8_t func   = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
        uint8_t offset = (uint8_t)(last_cfg_addr & 0xFC);

        if (dev == 3 && func == 0 && offset / 4 < 16)
            rtl_pci_cfg[offset / 4] = val;
    }
}

uint32_t inl(uint16_t port)
{
    if (port != PCI_CONFIG_DATA)
        return 0xFFFFFFFF;
    if (!(last_cfg_addr & PCI_ADDR_ENABLE))
        return 0xFFFFFFFF;

    uint8_t dev    = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
    uint8_t func   = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
    uint8_t offset = (uint8_t)(last_cfg_addr & 0xFC);

    /* Only device 3, function 0 exists */
    if (dev == 3 && func == 0) {
        if (offset / 4 < 16)
            return rtl_pci_cfg[offset / 4];
        return 0;
    }

    /* All other slots: no device */
    return 0xFFFFFFFF;
}

/* ========================================================================
 * Helper: Set up a known MAC and make reset succeed
 * ======================================================================== */

static void setup_mock_nic(void)
{
    memset(mock_mmio, 0, sizeof(mock_mmio));

    /* MAC address: DE:AD:BE:EF:CA:FE */
    /* IDR0 (offset 0x00): bytes 0-3 = DE AD BE EF */
    mock_mmio[RTL_IDR0 + 0] = 0xDE;
    mock_mmio[RTL_IDR0 + 1] = 0xAD;
    mock_mmio[RTL_IDR0 + 2] = 0xBE;
    mock_mmio[RTL_IDR0 + 3] = 0xEF;
    /* IDR4 (offset 0x04): bytes 4-5 = CA FE */
    mock_mmio[RTL_IDR4 + 0] = 0xCA;
    mock_mmio[RTL_IDR4 + 1] = 0xFE;

    /* CR register (offset 0x37): RST bit starts clear so reset succeeds */
    mock_mmio[RTL_CR] = 0x00;

    /* PHY Status (offset 0x6C): link up, 1000 Mbps, full duplex */
    mock_mmio[RTL_PHY_STATUS] = RTL_PHY_LINK_UP | RTL_PHY_SPEED_1000 |
                                RTL_PHY_FULL_DUPLEX;
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

/* ========================================================================
 * Test 1: PCI Probe
 * ======================================================================== */

static void test_pci_probe(void)
{
    pci_device_t devices[32];
    int count, found = 0;

    TEST("PCI probe -- find RTL8168 at bus 0 dev 3");

    setup_mock_pci();
    setup_mock_nic();

    count = pci_scan(devices, 32);
    if (count < 1) FAIL("pci_scan found 0 devices");

    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == RTL_VENDOR_ID &&
            devices[i].device_id == RTL_DEVICE_ID_8168) {
            found = 1;

            if (devices[i].bus != 0)    FAIL("expected bus 0");
            if (devices[i].device != 3) FAIL("expected device 3");
            if (devices[i].class_code != PCI_CLASS_NETWORK)
                FAIL("expected class 0x02 (network)");
            if (devices[i].subclass != PCI_SUBCLASS_ETHERNET)
                FAIL("expected subclass 0x00 (ethernet)");

            /* Verify BAR2 = MMIO base */
            uint64_t bar2 = pci_get_bar_address(&devices[i], 2);
            if (bar2 != MOCK_MMIO_BASE)
                FAIL("BAR2 address mismatch");

            if (!pci_is_bar_mmio(&devices[i], 2))
                FAIL("BAR2 should be MMIO");

            break;
        }
    }

    if (!found) FAIL("RTL8168 not found in scan");
    PASS();
}

/* ========================================================================
 * Test 2: MAC Address Read
 * ======================================================================== */

static void test_mac_address_read(void)
{
    rtl_nic_t nic;
    uint8_t mac[6];
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));

    TEST("MAC address read -- DE:AD:BE:EF:CA:FE");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) FAIL("rtl_nic_init failed");

    rtl_nic_get_mac(&nic, mac);

    if (mac[0] != 0xDE) FAIL("mac[0] != 0xDE");
    if (mac[1] != 0xAD) FAIL("mac[1] != 0xAD");
    if (mac[2] != 0xBE) FAIL("mac[2] != 0xBE");
    if (mac[3] != 0xEF) FAIL("mac[3] != 0xEF");
    if (mac[4] != 0xCA) FAIL("mac[4] != 0xCA");
    if (mac[5] != 0xFE) FAIL("mac[5] != 0xFE");

    PASS();
}

/* ========================================================================
 * Test 3: TX Descriptor Build
 * ======================================================================== */

static void test_tx_descriptor_build(void)
{
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));
    rtl_nic_t nic;
    uint8_t frame[64];
    uint32_t opts1;

    TEST("TX descriptor build -- OWN, FS, LS, length, EOR");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) FAIL("rtl_nic_init failed");

    /* After init, all TX descriptors should be host-owned (OWN=0) */
    if (tx_ring[0].opts1 & RTL_TX_OWN)
        FAIL("TX desc 0 OWN should be clear after init");

    /* Last descriptor should have EOR bit set */
    if (!(tx_ring[RTL_TX_RING_SIZE - 1].opts1 & RTL_TX_EOR))
        FAIL("TX desc 255 should have EOR bit set");

    /* Send a 64-byte frame */
    memset(frame, 0xAA, sizeof(frame));
    rc = rtl_nic_send(&nic, frame, 64);
    if (rc != 0) FAIL("rtl_nic_send failed");

    /* Check descriptor 0 after send */
    opts1 = tx_ring[0].opts1;

    if (!(opts1 & RTL_TX_OWN)) FAIL("TX desc 0 OWN not set after send");
    if (!(opts1 & RTL_TX_FS))  FAIL("TX desc 0 FS not set");
    if (!(opts1 & RTL_TX_LS))  FAIL("TX desc 0 LS not set");

    uint32_t desc_len = opts1 & RTL_TX_LEN_MASK;
    if (desc_len != 64) FAIL("TX desc 0 length != 64");

    /* Descriptor 0 is NOT the last, so EOR should NOT be set */
    if (opts1 & RTL_TX_EOR) FAIL("TX desc 0 should not have EOR");

    /* tx_head should have advanced to 1 */
    if (nic.tx_head != 1) FAIL("tx_head should be 1");

    /* Verify TX poll was kicked (check TPPOLL register in mock) */
    if (!(mock_mmio[RTL_TPPOLL] & RTL_TPPOLL_NPQ))
        FAIL("TPPOLL NPQ bit not set after send");

    PASS();
}

/* ========================================================================
 * Test 4: RX Ring Wrap
 * ======================================================================== */

static void test_rx_ring_wrap(void)
{
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));
    rtl_nic_t nic;

    TEST("RX ring wrap -- index wraps at 256");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) FAIL("rtl_nic_init failed");

    /* After init, all RX descriptors should have OWN=1 */
    for (int i = 0; i < RTL_RX_RING_SIZE; i++) {
        if (!(rx_ring[i].opts1 & RTL_RX_OWN))
            FAIL("RX desc should have OWN=1 after init");
    }

    /* Last RX descriptor should have EOR */
    if (!(rx_ring[RTL_RX_RING_SIZE - 1].opts1 & RTL_RX_EOR))
        FAIL("RX desc 255 should have EOR");

    /* First RX descriptor should NOT have EOR */
    if (rx_ring[0].opts1 & RTL_RX_EOR)
        FAIL("RX desc 0 should not have EOR");

    /* Simulate receiving frames to test wraparound:
     * Clear OWN and set length for descriptors 253, 254, 255, 0, 1 */
    nic.rx_cur = 253;

    for (int i = 0; i < 5; i++) {
        uint32_t idx = (253 + i) % RTL_RX_RING_SIZE;
        uint32_t eor = (idx == RTL_RX_RING_SIZE - 1) ? RTL_RX_EOR : 0;

        /* Simulate NIC writing a 100-byte frame (including 4-byte FCS) */
        rx_ring[idx].opts1 = RTL_RX_FS | RTL_RX_LS | eor | 104;  /* OWN=0 */
    }

    /* Receive 5 frames, crossing the ring boundary */
    uint8_t buf[2048];
    for (int i = 0; i < 5; i++) {
        int len = rtl_nic_recv(&nic, buf, sizeof(buf));
        if (len < 0) FAIL("rtl_nic_recv returned error");
        /* len should be 100 (104 - 4 FCS) */
        if (len != 100) FAIL("expected frame length 100");
    }

    /* rx_cur should have wrapped: (253 + 5) % 256 = 2 */
    if (nic.rx_cur != 2) FAIL("rx_cur should be 2 after wrapping");

    /* Re-armed descriptors should have OWN=1 again */
    for (int i = 0; i < 5; i++) {
        uint32_t idx = (253 + i) % RTL_RX_RING_SIZE;
        if (!(rx_ring[idx].opts1 & RTL_RX_OWN))
            FAIL("Re-armed RX desc should have OWN=1");
    }

    /* Descriptor 255 should still have EOR after re-arm */
    if (!(rx_ring[255].opts1 & RTL_RX_EOR))
        FAIL("Desc 255 should have EOR after re-arm");

    PASS();
}

/* ========================================================================
 * Test 5: Link Status
 * ======================================================================== */

static void test_link_status(void)
{
    rtl_nic_t nic;
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));

    TEST("Link status -- up/down, speed detection");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) FAIL("rtl_nic_init failed");

    /* After init with mock PHY = link up + 1000M + FD */
    if (!nic.link_up) FAIL("link should be up");
    if (nic.link_speed != 1000) FAIL("speed should be 1000");
    if (!nic.full_duplex) FAIL("should be full duplex");

    /* Change PHY to 100 Mbps half duplex */
    mock_mmio[RTL_PHY_STATUS] = RTL_PHY_LINK_UP | RTL_PHY_SPEED_100;
    rtl_nic_link_status(&nic);
    if (!nic.link_up) FAIL("link should still be up");
    if (nic.link_speed != 100) FAIL("speed should be 100");
    if (nic.full_duplex) FAIL("should be half duplex");

    /* Change PHY to link down */
    mock_mmio[RTL_PHY_STATUS] = 0x00;
    rc = rtl_nic_link_status(&nic);
    if (rc != 0) FAIL("link_status should return 0 for down");
    if (nic.link_up) FAIL("link should be down");
    if (nic.link_speed != 0) FAIL("speed should be 0 when down");

    /* Change PHY to 10 Mbps full duplex */
    mock_mmio[RTL_PHY_STATUS] = RTL_PHY_LINK_UP | RTL_PHY_SPEED_10 |
                                RTL_PHY_FULL_DUPLEX;
    rtl_nic_link_status(&nic);
    if (!nic.link_up) FAIL("link should be up");
    if (nic.link_speed != 10) FAIL("speed should be 10");
    if (!nic.full_duplex) FAIL("should be full duplex");

    PASS();
}

/* ========================================================================
 * Test 6: Send Basic
 * ======================================================================== */

static void test_send_basic(void)
{
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));
    rtl_nic_t nic;

    TEST("Send basic -- multi-frame TX, ring full detection");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) FAIL("rtl_nic_init failed");

    /* Send 3 frames of different sizes */
    uint8_t frame1[64], frame2[128], frame3[1500];
    memset(frame1, 0x11, sizeof(frame1));
    memset(frame2, 0x22, sizeof(frame2));
    memset(frame3, 0x33, sizeof(frame3));

    rc = rtl_nic_send(&nic, frame1, 64);
    if (rc != 0) FAIL("send frame1 failed");

    rc = rtl_nic_send(&nic, frame2, 128);
    if (rc != 0) FAIL("send frame2 failed");

    rc = rtl_nic_send(&nic, frame3, 1500);
    if (rc != 0) FAIL("send frame3 failed");

    /* Verify tx_head advanced by 3 */
    if (nic.tx_head != 3) FAIL("tx_head should be 3");

    /* Verify frame lengths in descriptors */
    if ((tx_ring[0].opts1 & RTL_TX_LEN_MASK) != 64)
        FAIL("desc 0 length != 64");
    if ((tx_ring[1].opts1 & RTL_TX_LEN_MASK) != 128)
        FAIL("desc 1 length != 128");
    if ((tx_ring[2].opts1 & RTL_TX_LEN_MASK) != 1500)
        FAIL("desc 2 length != 1500");

    /* Verify all 3 have OWN+FS+LS */
    for (int i = 0; i < 3; i++) {
        if (!(tx_ring[i].opts1 & RTL_TX_OWN)) FAIL("missing OWN");
        if (!(tx_ring[i].opts1 & RTL_TX_FS))  FAIL("missing FS");
        if (!(tx_ring[i].opts1 & RTL_TX_LS))  FAIL("missing LS");
    }

    /* Verify statistics */
    if (nic.tx_packets != 3) FAIL("tx_packets should be 3");
    if (nic.tx_bytes != 64 + 128 + 1500)
        FAIL("tx_bytes mismatch");

    /* Reject oversized frame */
    uint8_t big[2048];
    rc = rtl_nic_send(&nic, big, 2048);
    if (rc == 0) FAIL("should reject frame > RTL_MAX_FRAME_SIZE");

    /* Reject zero-length frame */
    rc = rtl_nic_send(&nic, frame1, 0);
    if (rc == 0) FAIL("should reject zero-length frame");

    /* Reject NULL buffer */
    rc = rtl_nic_send(&nic, NULL, 64);
    if (rc == 0) FAIL("should reject NULL buffer");

    /* Verify TX on last ring entry sets EOR */
    nic.tx_head = RTL_TX_RING_SIZE - 1;
    /* Clear the last descriptor so it's host-owned */
    tx_ring[RTL_TX_RING_SIZE - 1].opts1 = RTL_TX_EOR;  /* EOR only, OWN=0 */
    rc = rtl_nic_send(&nic, frame1, 64);
    if (rc != 0) FAIL("send on last entry failed");
    if (!(tx_ring[RTL_TX_RING_SIZE - 1].opts1 & RTL_TX_EOR))
        FAIL("last TX desc should have EOR");
    if (nic.tx_head != 0)
        FAIL("tx_head should wrap to 0");

    PASS();
}

/* ========================================================================
 * Test 7: RX Frame Delivery
 * ======================================================================== */

static void test_rx_frame_delivery(void)
{
    static rtl_tx_desc_t tx_ring[RTL_TX_RING_SIZE] __attribute__((aligned(256)));
    static rtl_rx_desc_t rx_ring[RTL_RX_RING_SIZE] __attribute__((aligned(256)));
    rtl_nic_t nic;

    TEST("RX frame delivery -- data copied to caller buffer");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = rtl_nic_init(&nic, MOCK_MMIO_BASE);
    if (rc != 0) { FAIL("rtl_nic_init failed"); return; }

    /* Allocate a fake RX buffer and assign it to descriptor 0 */
    static uint8_t rxbuf[RTL_RX_BUF_SIZE];
    memset(rxbuf, 0, sizeof(rxbuf));

    /* Write a test Ethernet frame into the buffer */
    const uint8_t test_frame[] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  /* dst MAC (broadcast) */
        0xDE,0xAD,0xBE,0xEF,0x00,0x01,  /* src MAC */
        0x08,0x00,                        /* EtherType: IPv4 */
        0x45,0x00,0x00,0x1C,             /* IPv4 header stub */
    };
    memcpy(rxbuf, test_frame, sizeof(test_frame));

    rtl_nic_set_rx_buffer(&nic, 0, rxbuf, (uint64_t)(uintptr_t)rxbuf,
                           RTL_RX_BUF_SIZE);

    /* Mark descriptor 0 as NIC-complete: OWN=0, FS+LS, length=68 (64 data + 4 FCS) */
    rx_ring[0].opts1 = RTL_RX_FS | RTL_RX_LS | 68;

    /* Receive */
    uint8_t outbuf[2048];
    memset(outbuf, 0xCC, sizeof(outbuf));
    int len = rtl_nic_recv(&nic, outbuf, sizeof(outbuf));

    if (len != 64) { FAIL("expected 64 bytes (68 - 4 FCS)"); return; }
    if (memcmp(outbuf, test_frame, sizeof(test_frame)) != 0) {
        FAIL("frame data mismatch");
        return;
    }
    if (nic.rx_packets != 1) { FAIL("rx_packets should be 1"); return; }

    /* Descriptor should be re-armed with OWN=1 */
    if (!(rx_ring[0].opts1 & RTL_RX_OWN)) {
        FAIL("descriptor should be re-armed with OWN");
        return;
    }

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== JARVIS AI-OS: RTL8168/8111 NIC Driver Tests ===\n\n");

    test_pci_probe();
    test_mac_address_read();
    test_tx_descriptor_build();
    test_rx_ring_wrap();
    test_link_status();
    test_send_basic();
    test_rx_frame_delivery();

    printf("\n=== Results: %d/%d PASSED ===\n", tests_passed, tests_run);

    if (tests_passed == tests_run)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
