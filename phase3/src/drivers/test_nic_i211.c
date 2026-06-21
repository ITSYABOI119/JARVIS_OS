/**
 * test_nic_i211.c - Intel I211-AT NIC Driver Unit Tests
 *
 * Mock MMIO and PCI tests for the I211 NIC driver.
 * Simulates an Intel 8086:1539 device on PCI bus 7 / device 0.
 *
 * Tests:
 *   1.  PCI probe -- find I211-AT via vendor:device ID
 *   2.  MAC read -- verify 6-byte extraction from RAL0/RAH0
 *   3.  Init reset -- verify CTRL gets RST written
 *   4.  Init link up -- verify CTRL has SLU|ASDE
 *   5.  Init RX enabled -- verify RCTL has EN|BAM|SECRC
 *   6.  Init TX enabled -- verify TCTL has EN|PSP
 *   7.  Init interrupts disabled -- verify IMC was 0xFFFFFFFF
 *   8.  TX descriptor -- send 64 bytes, verify desc fields
 *   9.  TX doorbell -- verify TDT advanced
 *   10. RX receive -- pre-set DD+EOP+length, verify recv
 *   11. Link status -- STATUS with LU+1000+FD, verify struct
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers phase3/src/drivers/test_nic_i211.c \
 *       phase3/src/drivers/nic_i211.c phase3/src/drivers/pci.c \
 *       -o /tmp/test_nic_i211 && /tmp/test_nic_i211
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nic_i211.h"
#include "pci.h"

/* ========================================================================
 * Mock MMIO Layer
 *
 * The I211 register space extends to 0xE028+, so we need 64KB of mock
 * memory. nic_i211.c calls nic_read32(base, reg) and nic_write32(base, reg, val)
 * where base is the MMIO base address. We set base=0 so reg is the
 * direct index into mock_mmio[].
 * ======================================================================== */

#define MOCK_MMIO_BASE  0       /* base=0 so reg offset == array index */
#define MOCK_MMIO_SIZE  65536   /* 64KB covers all I211 registers */

static uint8_t mock_mmio[MOCK_MMIO_SIZE];

/* Track writes for verification */
static uint32_t last_imc_write = 0;
static int      imc_write_count = 0;
static int      ctrl_rst_written = 0;

uint32_t nic_read32(uint64_t base, uint32_t reg)
{
    (void)base;
    if (reg + 4 <= MOCK_MMIO_SIZE) {
        uint32_t val;
        memcpy(&val, &mock_mmio[reg], sizeof(val));

        /* Simulate hardware auto-clearing RST bit after read */
        if (reg == I211_CTRL && (val & I211_CTRL_RST)) {
            val &= ~I211_CTRL_RST;
            memcpy(&mock_mmio[reg], &val, sizeof(val));
        }

        return val;
    }
    return 0;
}

void nic_write32(uint64_t base, uint32_t reg, uint32_t val)
{
    (void)base;
    if (reg + 4 <= MOCK_MMIO_SIZE) {
        memcpy(&mock_mmio[reg], &val, sizeof(val));

        /* Track IMC writes */
        if (reg == I211_IMC) {
            last_imc_write = val;
            imc_write_count++;
        }

        /* Track CTRL RST writes */
        if (reg == I211_CTRL && (val & I211_CTRL_RST))
            ctrl_rst_written = 1;
    }
}

/* ========================================================================
 * Mock PCI I/O Port Layer
 *
 * Simulate PCI config space for 32 device slots on bus 7.
 * Only bus 7 / device 0 has an Intel I211-AT. All others return 0xFFFF.
 * ======================================================================== */

static uint32_t i211_pci_cfg[16];
static uint32_t last_cfg_addr = 0;

static void setup_mock_pci(void)
{
    memset(i211_pci_cfg, 0, sizeof(i211_pci_cfg));

    /* Bus 7, Device 0: Intel I211-AT GbE */
    i211_pci_cfg[0]  = (I211_DEVICE_ID << 16) | I211_VENDOR_ID; /* 8086:1539 */
    i211_pci_cfg[1]  = 0x00000007;  /* command: IO + Mem + BusMaster */
    i211_pci_cfg[2]  = 0x02000000;  /* class=0x02(network), sub=0x00(ethernet) */
    i211_pci_cfg[3]  = 0x00000000;  /* header_type=0x00 */
    i211_pci_cfg[4]  = MOCK_MMIO_BASE;  /* BAR0: MMIO at 0 (bit0=0 for Mem) */
    i211_pci_cfg[5]  = 0x00000000;  /* BAR1 */
    i211_pci_cfg[6]  = 0x00000000;  /* BAR2 */
    i211_pci_cfg[15] = 0x00000A01;  /* int_line=10, int_pin=1(INTA) */
}

/* PCI I/O port mock: outl/inl used by pci.c */

void outl(uint16_t port, uint32_t val)
{
    if (port == PCI_CONFIG_ADDRESS)
        last_cfg_addr = val;
    else if (port == PCI_CONFIG_DATA) {
        if (!(last_cfg_addr & PCI_ADDR_ENABLE))
            return;

        uint8_t bus    = (uint8_t)((last_cfg_addr >> 16) & 0xFF);
        uint8_t dev    = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
        uint8_t func   = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
        uint8_t offset = (uint8_t)(last_cfg_addr & 0xFC);

        if (bus == 7 && dev == 0 && func == 0 && offset / 4 < 16)
            i211_pci_cfg[offset / 4] = val;
    }
}

uint32_t inl(uint16_t port)
{
    if (port != PCI_CONFIG_DATA)
        return 0xFFFFFFFF;
    if (!(last_cfg_addr & PCI_ADDR_ENABLE))
        return 0xFFFFFFFF;

    uint8_t bus    = (uint8_t)((last_cfg_addr >> 16) & 0xFF);
    uint8_t dev    = (uint8_t)((last_cfg_addr >> 11) & 0x1F);
    uint8_t func   = (uint8_t)((last_cfg_addr >> 8)  & 0x07);
    uint8_t offset = (uint8_t)(last_cfg_addr & 0xFC);

    /* Only bus 7, device 0, function 0 exists */
    if (bus == 7 && dev == 0 && func == 0) {
        if (offset / 4 < 16)
            return i211_pci_cfg[offset / 4];
        return 0;
    }

    /* All other slots: no device */
    return 0xFFFFFFFF;
}

/* ========================================================================
 * Helper: Set up known MAC and make reset succeed
 * ======================================================================== */

static void setup_mock_nic(void)
{
    uint32_t ral, rah;

    memset(mock_mmio, 0, sizeof(mock_mmio));
    last_imc_write = 0;
    imc_write_count = 0;
    ctrl_rst_written = 0;

    /* MAC address in RAL0/RAH0: 11:22:33:44:55:66 */
    ral = 0x44332211;  /* bytes 0-3 in little-endian */
    rah = 0x80006655;  /* AV bit (31) + bytes 4-5 in little-endian */
    memcpy(&mock_mmio[I211_RAL0], &ral, sizeof(ral));
    memcpy(&mock_mmio[I211_RAH0], &rah, sizeof(rah));

    /* STATUS: link up, 1000 Mbps, full duplex, PF reset done (B3 settle poll) */
    {
        uint32_t status = I211_STATUS_LU | I211_STATUS_SPEED_1000 |
                          I211_STATUS_FD | I211_STATUS_PF_RST_DONE;
        memcpy(&mock_mmio[I211_STATUS], &status, sizeof(status));
    }
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) do { \
    tests_run++; \
    printf("Test %2d: %-45s ", tests_run, name); \
} while (0)

#define TEST_PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while (0)

/* ========================================================================
 * Shared ring storage (static to avoid stack overflow)
 * ======================================================================== */

static i211_tx_desc_t tx_ring[I211_TX_RING_SIZE] __attribute__((aligned(256)));
static i211_rx_desc_t rx_ring[I211_RX_RING_SIZE] __attribute__((aligned(256)));
static uint8_t rx_buffer[I211_RX_BUF_SIZE];

/* ========================================================================
 * Test 1: PCI Probe
 * ======================================================================== */

static void test_pci_probe(void)
{
    pci_device_t devices[32];
    int count, found = 0;

    TEST_START("PCI probe -- find I211-AT 8086:1539");

    setup_mock_pci();
    setup_mock_nic();

    count = pci_scan(devices, 32);
    EXPECT(count >= 1, "pci_scan found 0 devices");

    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == I211_VENDOR_ID &&
            devices[i].device_id == I211_DEVICE_ID) {
            found = 1;
            EXPECT(devices[i].bus == 7, "expected bus 7");
            EXPECT(devices[i].device == 0, "expected device 0");
            EXPECT(devices[i].class_code == PCI_CLASS_NETWORK,
                   "expected class 0x02 (network)");
            EXPECT(devices[i].subclass == PCI_SUBCLASS_ETHERNET,
                   "expected subclass 0x00 (ethernet)");

            uint64_t bar0 = pci_get_bar_address(&devices[i], 0);
            EXPECT(bar0 == MOCK_MMIO_BASE, "BAR0 address mismatch");
            EXPECT(pci_is_bar_mmio(&devices[i], 0), "BAR0 should be MMIO");
            break;
        }
    }

    EXPECT(found, "I211-AT not found in scan");
    TEST_PASS();
}

/* ========================================================================
 * Test 2: MAC Address Read
 * ======================================================================== */

static void test_mac_read(void)
{
    i211_nic_t nic;
    uint8_t mac[6];

    TEST_START("MAC read -- RAL0/RAH0 extraction");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    i211_nic_get_mac(&nic, mac);

    EXPECT(mac[0] == 0x11, "mac[0] != 0x11");
    EXPECT(mac[1] == 0x22, "mac[1] != 0x22");
    EXPECT(mac[2] == 0x33, "mac[2] != 0x33");
    EXPECT(mac[3] == 0x44, "mac[3] != 0x44");
    EXPECT(mac[4] == 0x55, "mac[4] != 0x55");
    EXPECT(mac[5] == 0x66, "mac[5] != 0x66");

    /* B3: mock RAH0 has AV set + a non-zero/non-FF MAC -> mac_valid must be 1 */
    EXPECT(nic.mac_valid == 1, "mac_valid should be 1 (AV set + valid MAC)");

    TEST_PASS();
}

/* ========================================================================
 * Test 3: Init Reset
 * ======================================================================== */

static void test_init_reset(void)
{
    i211_nic_t nic;

    TEST_START("Init reset -- CTRL gets RST written");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");
    EXPECT(ctrl_rst_written == 1, "CTRL RST was not written");

    TEST_PASS();
}

/* ========================================================================
 * Test 4: Init Link Up
 * ======================================================================== */

static void test_init_link_up(void)
{
    i211_nic_t nic;
    uint32_t ctrl;

    TEST_START("Init link up -- CTRL has SLU|ASDE");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    memcpy(&ctrl, &mock_mmio[I211_CTRL], sizeof(ctrl));
    EXPECT(ctrl & I211_CTRL_SLU, "CTRL missing SLU");
    EXPECT(ctrl & I211_CTRL_ASDE, "CTRL missing ASDE");

    TEST_PASS();
}

/* ========================================================================
 * Test 5: Init RX Enabled
 * ======================================================================== */

static void test_init_rx_enabled(void)
{
    i211_nic_t nic;
    uint32_t rctl;

    TEST_START("Init RX enabled -- RCTL has EN|BAM|SECRC");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    memcpy(&rctl, &mock_mmio[I211_RCTL], sizeof(rctl));
    EXPECT(rctl & I211_RCTL_EN, "RCTL missing EN");
    EXPECT(rctl & I211_RCTL_BAM, "RCTL missing BAM");
    EXPECT(rctl & I211_RCTL_SECRC, "RCTL missing SECRC");

    TEST_PASS();
}

/* ========================================================================
 * Test 6: Init TX Enabled
 * ======================================================================== */

static void test_init_tx_enabled(void)
{
    i211_nic_t nic;
    uint32_t tctl;

    TEST_START("Init TX enabled -- TCTL has EN|PSP");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    memcpy(&tctl, &mock_mmio[I211_TCTL], sizeof(tctl));
    EXPECT(tctl & I211_TCTL_EN, "TCTL missing EN");
    EXPECT(tctl & I211_TCTL_PSP, "TCTL missing PSP");

    /* B2: the per-queue TX enable (TXDCTL.ENABLE) must be written — #1 silent-TX cause */
    {
        uint32_t txdctl;
        memcpy(&txdctl, &mock_mmio[I211_TXDCTL], sizeof(txdctl));
        EXPECT(txdctl & I211_TXDCTL_ENABLE, "TXDCTL missing ENABLE (B2)");
    }

    TEST_PASS();
}

/* ========================================================================
 * Test 7: Init Interrupts Disabled
 * ======================================================================== */

static void test_init_interrupts_disabled(void)
{
    i211_nic_t nic;

    TEST_START("Init interrupts disabled -- IMC=0xFFFFFFFF");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    /* IMC should have been written with 0xFFFFFFFF at least twice
     * (once before reset, once after) */
    EXPECT(last_imc_write == 0xFFFFFFFF, "IMC not written with 0xFFFFFFFF");
    EXPECT(imc_write_count >= 2, "IMC should be written at least twice");

    TEST_PASS();
}

/* ========================================================================
 * Test 8: TX Descriptor
 * ======================================================================== */

static void test_tx_descriptor(void)
{
    i211_nic_t nic;
    uint8_t txbuf[64];
    uint8_t frame[64];
    /* A PHYSICAL address distinct from any virtual address — proves the descriptor
     * carries the supplied PHYSICAL addr, not the vaddr of the buffer (B1). */
    const uint64_t FAKE_PADDR = 0x00000000DEAD0000ULL;

    TEST_START("TX descriptor (phys) -- desc.addr == PHYSICAL addr (B1)");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    /* Send a 64-byte frame via the paddr-aware path */
    memset(frame, 0xAA, sizeof(frame));
    memset(txbuf, 0, sizeof(txbuf));
    rc = i211_send_phys(&nic, txbuf, FAKE_PADDR, frame, 64);
    EXPECT(rc == 0, "i211_send_phys should return desc idx 0");

    /* B1: descriptor addr must be the PHYSICAL address, NOT the vaddr of the buffer */
    EXPECT(tx_ring[0].addr == FAKE_PADDR, "TX desc addr must be the PHYSICAL addr");
    EXPECT(tx_ring[0].addr != (uint64_t)(uintptr_t)txbuf,
           "TX desc addr must NOT be the vaddr");
    EXPECT(memcmp(txbuf, frame, 64) == 0, "frame must be copied into the DMA TX buffer");
    EXPECT(tx_ring[0].length == 64, "TX desc length != 64");
    EXPECT(tx_ring[0].cmd & I211_TX_CMD_EOP, "TX desc missing EOP");
    EXPECT(tx_ring[0].cmd & I211_TX_CMD_IFCS, "TX desc missing IFCS");
    EXPECT(tx_ring[0].cmd & I211_TX_CMD_RS, "TX desc missing RS");
    EXPECT(tx_ring[0].sta == 0, "TX desc sta should be 0");

    /* Stats */
    EXPECT(nic.tx_packets == 1, "tx_packets should be 1");
    EXPECT(nic.tx_bytes == 64, "tx_bytes should be 64");

    TEST_PASS();
}

/* ========================================================================
 * Test 9: TX Doorbell
 * ======================================================================== */

static void test_tx_doorbell(void)
{
    i211_nic_t nic;
    uint8_t frame[64];
    uint32_t tdt;

    TEST_START("TX doorbell -- TDT advanced after send");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    /* TDT should be 0 after init */
    memcpy(&tdt, &mock_mmio[I211_TDT], sizeof(tdt));
    EXPECT(tdt == 0, "TDT should be 0 after init");

    /* Send a frame */
    memset(frame, 0xBB, sizeof(frame));
    rc = i211_nic_send(&nic, frame, 64);
    EXPECT(rc == 0, "i211_nic_send failed");

    /* TDT should now be 1 */
    memcpy(&tdt, &mock_mmio[I211_TDT], sizeof(tdt));
    EXPECT(tdt == 1, "TDT should be 1 after first send");

    /* tx_head should also be 1 */
    EXPECT(nic.tx_head == 1, "tx_head should be 1");

    /* Send another frame */
    rc = i211_nic_send(&nic, frame, 128);
    EXPECT(rc == 0, "second send failed");

    memcpy(&tdt, &mock_mmio[I211_TDT], sizeof(tdt));
    EXPECT(tdt == 2, "TDT should be 2 after second send");

    TEST_PASS();
}

/* ========================================================================
 * Test 10: RX Receive
 * ======================================================================== */

static void test_rx_receive(void)
{
    i211_nic_t nic;
    uint8_t outbuf[2048];
    int len;

    TEST_START("RX receive -- DD+EOP+length, data copied");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    /* Assign RX buffer to descriptor 0 */
    memset(rx_buffer, 0, sizeof(rx_buffer));

    /* Write test frame into RX buffer */
    const uint8_t test_frame[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* dst MAC (broadcast) */
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  /* src MAC */
        0x08, 0x00,                            /* EtherType: IPv4 */
        0x45, 0x00, 0x00, 0x1C,               /* IPv4 header stub */
    };
    memcpy(rx_buffer, test_frame, sizeof(test_frame));

    i211_nic_set_rx_buffer(&nic, 0, rx_buffer,
                            (uint64_t)(uintptr_t)rx_buffer,
                            I211_RX_BUF_SIZE);

    /* Simulate NIC completing descriptor 0: DD+EOP, 100 bytes received */
    rx_ring[0].status = I211_RX_STA_DD | I211_RX_STA_EOP;
    rx_ring[0].length = 100;
    rx_ring[0].errors = 0;

    /* Receive */
    memset(outbuf, 0xCC, sizeof(outbuf));
    len = i211_nic_recv(&nic, outbuf, sizeof(outbuf));

    EXPECT(len == 100, "expected 100 bytes");
    EXPECT(memcmp(outbuf, test_frame, sizeof(test_frame)) == 0,
           "frame data mismatch");
    EXPECT(nic.rx_packets == 1, "rx_packets should be 1");
    EXPECT(nic.rx_bytes == 100, "rx_bytes should be 100");

    /* Descriptor should be cleared for reuse */
    EXPECT(rx_ring[0].status == 0, "descriptor status should be cleared");

    /* rx_cur should advance to 1 */
    EXPECT(nic.rx_cur == 1, "rx_cur should be 1");

    /* No frame available on next call */
    len = i211_nic_recv(&nic, outbuf, sizeof(outbuf));
    EXPECT(len == 0, "should return 0 when no frame available");

    TEST_PASS();
}

/* ========================================================================
 * Test 11: Link Status
 * ======================================================================== */

static void test_link_status(void)
{
    i211_nic_t nic;
    uint32_t status;

    TEST_START("Link status -- LU+1000+FD from STATUS reg");

    setup_mock_nic();

    memset(&nic, 0, sizeof(nic));
    nic.tx_ring = tx_ring;
    nic.rx_ring = rx_ring;

    int rc = i211_nic_init(&nic, MOCK_MMIO_BASE);
    EXPECT(rc == 0, "i211_nic_init failed");

    /* After init with STATUS = LU + 1000M + FD */
    EXPECT(nic.link_up == 1, "link should be up");
    EXPECT(nic.link_speed == 1000, "speed should be 1000");
    EXPECT(nic.full_duplex == 1, "should be full duplex");

    /* Change STATUS to 100 Mbps, half duplex, link up */
    status = I211_STATUS_LU | I211_STATUS_SPEED_100;
    memcpy(&mock_mmio[I211_STATUS], &status, sizeof(status));
    i211_nic_link_status(&nic);
    EXPECT(nic.link_up == 1, "link should still be up");
    EXPECT(nic.link_speed == 100, "speed should be 100");
    EXPECT(nic.full_duplex == 0, "should be half duplex");

    /* Change STATUS to link down */
    status = 0;
    memcpy(&mock_mmio[I211_STATUS], &status, sizeof(status));
    rc = i211_nic_link_status(&nic);
    EXPECT(rc == 0, "link_status should return 0 for down");
    EXPECT(nic.link_up == 0, "link should be down");
    EXPECT(nic.link_speed == 10, "speed field should be 10 (SPEED_10=0)");

    /* Change STATUS to 10 Mbps, full duplex */
    status = I211_STATUS_LU | I211_STATUS_SPEED_10 | I211_STATUS_FD;
    memcpy(&mock_mmio[I211_STATUS], &status, sizeof(status));
    i211_nic_link_status(&nic);
    EXPECT(nic.link_up == 1, "link should be up");
    EXPECT(nic.link_speed == 10, "speed should be 10");
    EXPECT(nic.full_duplex == 1, "should be full duplex");

    TEST_PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== JARVIS AI-OS: Intel I211-AT NIC Driver Tests ===\n\n");

    test_pci_probe();
    test_mac_read();
    test_init_reset();
    test_init_link_up();
    test_init_rx_enabled();
    test_init_tx_enabled();
    test_init_interrupts_disabled();
    test_tx_descriptor();
    test_tx_doorbell();
    test_rx_receive();
    test_link_status();

    printf("\n=== Results: %d/%d PASSED ===\n", tests_passed, tests_run);

    if (tests_passed == tests_run)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED (%d failures)\n", tests_failed);

    return (tests_passed == tests_run) ? 0 : 1;
}
