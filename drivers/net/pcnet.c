#include <drivers/pcnet.h>
#include <drivers/pci.h>
#include <drivers/drv_core.h>
#include <drivers/pit_timer.h>
#include <core/core.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <x86_io.h>
#include <x86_irq.h>
#include <x86_pic.h>
#include <iru_string.h>

/* AMD PCnet-PCI II (Am79C970A) Ethernet driver.
 *
 * Register model (mirrors the QEMU PCnet emulation, which follows the AMD
 * datasheet): the card lives on a 32-byte I/O BAR whose low 16 bytes are
 * the autoload PROM (APROM) holding the MAC, and whose high 16 bytes are
 * the register window. In DWIO mode that window is
 *   +0x10 RDP   register data port  (CSR read/write)
 *   +0x14 RAP   register address port (selects the CSR for RDP)
 *   +0x18 SRST  read = soft reset
 *   +0x1C BDP   BCR data port (RAP selects the BCR)
 * RAP is shared: CSR access is RAP + RDP, BCR access is RAP + BDP.
 *
 * Setup uses the 32-bit (PCnet-PCI) mode: BCR20 (SWS) = SWSTYLE 2, whose
 * write also sets the SSIZE32 bit, so the 32-bit init block and 16-byte
 * descriptors (natural field order) are used. The software ring holds a
 * single receive and a single transmit descriptor; with RCVRL == XMTRL
 * == 1 the hardware rings wrap to the base descriptor every time, keeping
 * the driver trivial while the card is exercised for real. */

#define PCNET_IOPORT_MASK  0x3u
#define PCNET_REG_RDP      0x10u
#define PCNET_REG_RAP      0x14u
#define PCNET_REG_SRST     0x18u
#define PCNET_REG_BDP      0x1Cu

/* Bus configuration registers. */
#define PCNET_BCR_BSBC     18u
#define PCNET_BCR_SWS      20u

/* CSR0. */
#define PCNET_CSR0_INIT    0x0001u
#define PCNET_CSR0_STRT    0x0002u
#define PCNET_CSR0_STOP    0x0004u
#define PCNET_CSR0_TDMD    0x0008u
#define PCNET_CSR0_INEA    0x0040u
#define PCNET_CSR0_IDON    0x0100u
#define PCNET_CSR0_TINT    0x0200u
#define PCNET_CSR0_RINT    0x0400u
#define PCNET_CSR0_IRQ_FLAGS 0x7F00u

/* CSR15 (mode) bits. */
#define PCNET_MODE_LOOP    0x0004u
#define PCNET_MODE_INTL    0x0040u

/* Descriptor fields (SWSTYLE 2, natural order, little endian). */
#define PCNET_ONES         0xF000u
#define PCNET_RMD_OWN      0x8000u
#define PCNET_RMD_STP      0x0200u
#define PCNET_RMD_ENP      0x0100u
#define PCNET_RMD_PAM      0x0040u
#define PCNET_TMD_OWN      0x8000u
#define PCNET_TMD_STP      0x0200u
#define PCNET_TMD_ENP      0x0100u
#define PCNET_TMD_ADDFCS   0x2000u

#define PCNET_RX_BUF_SIZE     2048u
#define PCNET_ETH_MIN_FRAME   60u
#define PCNET_INIT_TIMEOUT    100000u
#define PCNET_POLL_TIMEOUT    20000u

/* DMA page layout (one 8 KiB block). */
#define PCNET_DMA_OFF_INITBLK 0x000
#define PCNET_DMA_OFF_RMD     0x020
#define PCNET_DMA_OFF_TMD     0x030
#define PCNET_DMA_OFF_RX      0x040
#define PCNET_DMA_OFF_TX      0x840

struct pcnet_initblk32 {
    u16 mode;
    u8  rlen;                 /* (log2 rx entries) << 4 */
    u8  tlen;                 /* (log2 tx entries) << 4 */
    u8  padr[6];              /* MAC address */
    u16 res0;
    u8  ladrf[8];             /* logical address filter */
    u32 rdra;                 /* receive descriptor ring */
    u32 tdra;                 /* transmit descriptor ring */
} __packed;

struct pcnet_rmd {
    u32 rbadr;
    u16 buf_length;           /* ONES | (4096 - buffer size) */
    u16 status;
    u32 msg_length;           /* low 12 bits = received count */
    u32 res;
} __packed;

struct pcnet_tmd {
    u32 tbadr;
    u16 length;               /* ONES | (4096 - payload size) */
    u16 status;
    u32 misc;
    u32 res;
} __packed;

struct pcnet_dma {
    struct pcnet_initblk32 initblk;     /* 0x00 (28 bytes) */
    u32 pad;                            /* 0x1c            */
    struct pcnet_rmd rmd;               /* 0x20            */
    struct pcnet_tmd tmd;               /* 0x30            */
    u8  rx_buf[PCNET_RX_BUF_SIZE];      /* 0x40            */
    u8  tx_buf[PCNET_RX_BUF_SIZE];      /* 0x840           */
} __packed;

_Static_assert(sizeof(struct pcnet_initblk32) == 28,
               "pcnet init block layout");
_Static_assert(sizeof(struct pcnet_dma) == 0x1040,
               "pcnet DMA block layout");

struct pcnet_inst {
    u16 port;                 /* BAR0 I/O base */
    u8  bus;
    u8  pci_dev;
    u8  func;
    int irq;
    u8  mac[6];
    u32 dma_phys;
    struct pcnet_dma *dma;
    unsigned rx_irqs;
    unsigned tx_irqs;
    int present;
};

static struct pcnet_inst g_pcnet;

/* ------------------------------------------------------------------ *
 *  Register access (DWIO, 32-bit)
 * ------------------------------------------------------------------ */

static inline u16 pcnet_csr_read(unsigned int n)
{
    outl(g_pcnet.port + PCNET_REG_RAP, n);
    return (u16)inl(g_pcnet.port + PCNET_REG_RDP);
}

static inline void pcnet_csr_write(unsigned int n, u32 v)
{
    outl(g_pcnet.port + PCNET_REG_RAP, n);
    outl(g_pcnet.port + PCNET_REG_RDP, v);
}

static inline u16 pcnet_bcr_read(unsigned int n)
{
    outl(g_pcnet.port + PCNET_REG_RAP, n);
    return (u16)inl(g_pcnet.port + PCNET_REG_BDP);
}

static inline void pcnet_bcr_write(unsigned int n, u32 v)
{
    outl(g_pcnet.port + PCNET_REG_RAP, n);
    outl(g_pcnet.port + PCNET_REG_BDP, v);
}

/* ------------------------------------------------------------------ *
 *  Interrupt handler
 * ------------------------------------------------------------------ */

static void pcnet_irq_handler(void *arg)
{
    u16 csr0;

    (void)arg;
    if (!g_pcnet.present)
        return;

    csr0 = pcnet_csr_read(0);
    if (csr0 & PCNET_CSR0_RINT)
        g_pcnet.rx_irqs++;
    if (csr0 & PCNET_CSR0_TINT)
        g_pcnet.tx_irqs++;
    if (csr0 & PCNET_CSR0_IRQ_FLAGS)
        pcnet_csr_write(0, csr0 & PCNET_CSR0_IRQ_FLAGS);
}

/* ------------------------------------------------------------------ *
 *  Bring-up
 * ------------------------------------------------------------------ */

static void pcnet_soft_reset(void)
{
    /* Cards leave the factory in 16-bit I/O mode; the first 32-bit write
     * to RDP while non-DWIO flips the card into DWIO mode. Repeat the
     * dance after every soft reset because a reset clears DWIO again. */
    outl(g_pcnet.port + PCNET_REG_RDP, 0);
    (void)inl(g_pcnet.port + PCNET_REG_SRST);
    outl(g_pcnet.port + PCNET_REG_RDP, 0);
}

static int pcnet_hw_init(void)
{
    struct pcnet_dma *dma = g_pcnet.dma;
    u32 init_phys = g_pcnet.dma_phys;
    u16 w;
    u16 csr0;
    unsigned i;

    memset(dma, 0, sizeof(*dma));

    pcnet_soft_reset();

    /* SWSTYLE 2 (16-byte descriptors, natural order); the write also sets
     * the SSIZE32 bit, selecting the 32-bit init block. */
    pcnet_bcr_write(PCNET_BCR_SWS, 0x0002);

    /* The reset leaves the MAC in CSR12..CSR14: fold it into the init
     * block so the receive side can match frames addressed to us. */
    w = pcnet_csr_read(12);
    dma->initblk.padr[0] = (u8)w;
    dma->initblk.padr[1] = (u8)(w >> 8);
    w = pcnet_csr_read(13);
    dma->initblk.padr[2] = (u8)w;
    dma->initblk.padr[3] = (u8)(w >> 8);
    w = pcnet_csr_read(14);
    dma->initblk.padr[4] = (u8)w;
    dma->initblk.padr[5] = (u8)(w >> 8);

    dma->initblk.rlen = 0x00;              /* 1 << 0 = one RX descriptor */
    dma->initblk.tlen = 0x00;              /* 1 << 0 = one TX descriptor */
    dma->initblk.rdra = init_phys + PCNET_DMA_OFF_RMD;
    dma->initblk.tdra = init_phys + PCNET_DMA_OFF_TMD;

    /* Single receive descriptor, armed with a 2 KiB buffer. */
    dma->rmd.rbadr      = init_phys + PCNET_DMA_OFF_RX;
    dma->rmd.buf_length = PCNET_ONES | (u16)(4096 - PCNET_RX_BUF_SIZE);
    dma->rmd.status     = PCNET_RMD_OWN;
    dma->rmd.msg_length = 0;

    /* Point CSR1/CSR2 at the init block and INITIALIZE. */
    pcnet_csr_write(1, (u16)(init_phys & 0xFFFFu));
    pcnet_csr_write(2, (u16)(init_phys >> 16));
    pcnet_csr_write(3, 0);

    pcnet_csr_write(0, PCNET_CSR0_INIT);
    for (i = 0; i < PCNET_INIT_TIMEOUT; i++) {
        if (pcnet_csr_read(0) & PCNET_CSR0_IDON)
            break;
    }
    if (!(pcnet_csr_read(0) & PCNET_CSR0_IDON)) {
        pr_warn("pcnet: INIT timed out (csr0=%04x)\n", pcnet_csr_read(0));
        return -1;
    }
    pcnet_csr_write(0, PCNET_CSR0_IDON);   /* clear IDON */

    /* Run both directions and enable card interrupts. */
    pcnet_csr_write(0, PCNET_CSR0_STRT | PCNET_CSR0_INEA);

    memcpy(g_pcnet.mac, dma->initblk.padr, 6);
    csr0 = pcnet_csr_read(0);
    pr_info("pcnet: up, MAC %02x:%02x:%02x:%02x:%02x:%02x "
            "(csr0=%04x rxon=%d txon=%d)\n",
            g_pcnet.mac[0], g_pcnet.mac[1], g_pcnet.mac[2],
            g_pcnet.mac[3], g_pcnet.mac[4], g_pcnet.mac[5],
            csr0, !!(csr0 & 0x0020u), !!(csr0 & 0x0010u));
    return 0;
}

/* ------------------------------------------------------------------ *
 *  Loopback self-test (transmit -> internal loopback -> receive)
 * ------------------------------------------------------------------ */

static int pcnet_loopback_test(void)
{
    struct pcnet_rmd *rmd = &g_pcnet.dma->rmd;
    struct pcnet_tmd *tmd = &g_pcnet.dma->tmd;
    u8 *buf = g_pcnet.dma->tx_buf;
    const char payload[] = "NEWOS PCnet-NIC loopback self-test";
    u32 pkt_len = PCNET_ETH_MIN_FRAME;
    u32 rcvd;
    unsigned i;
    int rc = 0;

    /* 60-byte minimum frame to our own MAC. */
    memset(buf, 0, pkt_len);
    memcpy(buf, g_pcnet.mac, 6);            /* dst = our MAC */
    memcpy(buf + 6, g_pcnet.mac, 6);        /* src = our MAC */
    buf[12] = 0x08; buf[13] = 0x00;         /* ethertype IPv4 */
    memcpy(buf + 14, payload, sizeof(payload));

    tmd->tbadr  = g_pcnet.dma_phys + PCNET_DMA_OFF_TX;
    tmd->length = PCNET_ONES | (u16)(4096 - pkt_len);
    tmd->status = PCNET_TMD_OWN | PCNET_TMD_STP | PCNET_TMD_ENP |
                  PCNET_TMD_ADDFCS;
    tmd->misc   = 0;

    /* Internal loopback: the transmit feeds the receive path directly. */
    pcnet_csr_write(15, PCNET_MODE_LOOP | PCNET_MODE_INTL);

    g_pcnet.rx_irqs = 0;
    g_pcnet.tx_irqs = 0;
    pcnet_csr_write(0, PCNET_CSR0_TDMD);

    /* Writing TDMD also clears the INEA bit; re-assert it so the loopback
     * completion can raise card interrupts. */
    pcnet_csr_write(0, PCNET_CSR0_INEA);

    {
        u16 dbg0 = pcnet_csr_read(0);
        u16 dbg15 = pcnet_csr_read(15);
        pr_info("pcnet: dbg csr0=%04x csr15=%04x cxda=%04x%04x "
                "xmtrc=%u xmtrl=%u tmd.s=%04x tmd.m=%08x rmd.s=%04x\n",
                dbg0, dbg15,
                pcnet_csr_read(35), pcnet_csr_read(34),
                pcnet_csr_read(74), pcnet_csr_read(78),
                tmd->status, tmd->misc, rmd->status);
    }

    /* The loopback completes synchronously inside the TDMD write, but the
     * resulting IRQ is still in flight - poll, then give it a moment. */
    for (i = 0; i < PCNET_POLL_TIMEOUT && (rmd->status & PCNET_RMD_OWN);
         i++)
        time_delay_us(10);
    time_delay_us(100);

    if (rmd->status & PCNET_RMD_OWN) {
        pr_warn("pcnet: RX descriptor never released (status=%04x)\n",
                rmd->status);
        rc = -1;
        goto done;
    }
    if (!(rmd->status & PCNET_RMD_ENP)) {
        pr_warn("pcnet: RX frame incomplete (status=%04x)\n", rmd->status);
        rc = -1;
        goto done;
    }
    rcvd = rmd->msg_length & 0x0FFFu;
    if (rcvd != pkt_len + 4) {
        pr_warn("pcnet: RX length %u, expected %u\n", rcvd, pkt_len + 4);
        rc = -1;
        goto done;
    }
    if (memcmp(g_pcnet.dma->rx_buf, g_pcnet.mac, 6)) {
        pr_warn("pcnet: RX frame dst MAC mismatch\n");
        rc = -1;
        goto done;
    }
    if (!g_pcnet.rx_irqs && !g_pcnet.tx_irqs) {
        pr_warn("pcnet: no interrupt seen during loopback\n");
        rc = -1;
        goto done;
    }

    pr_info("pcnet: loopback OK (tx %u -> rx %u, irqs rx=%u tx=%u)\n",
            pkt_len, rcvd, g_pcnet.rx_irqs, g_pcnet.tx_irqs);

done:
    /* Ack any pending interrupt flags so the line goes quiet. */
    pcnet_csr_write(0, PCNET_CSR0_RINT | PCNET_CSR0_TINT);

    /* Leave the card running in normal (non-loopback) mode, RX re-armed. */
    pcnet_csr_write(15, 0);
    rmd->status     = 0;
    rmd->buf_length = PCNET_ONES | (u16)(4096 - PCNET_RX_BUF_SIZE);
    rmd->msg_length = 0;
    rmd->status     = PCNET_RMD_OWN;
    return rc;
}

int pcnet_selftest(void)
{
    if (!g_pcnet.present) {
        pr_info("pcnet: NIC not present, hardware test skipped\n");
        return 0;
    }
    if (pcnet_hw_init())
        return -1;
    return pcnet_loopback_test();
}

/* ------------------------------------------------------------------ *
 *  PCI binding
 * ------------------------------------------------------------------ */

static const struct device_id pcnet_ids[] = {
    { 0x1022, 0x2000, 0 },   /* AMD PCnet-PCI II (Am79C970A / 79C973) */
    { 0, 0, 0 },
};

static int pcnet_probe(struct device *dev)
{
    struct pci_dev_info *inf = dev->private;
    u32 bar0;
    u16 cmd;
    u8 line;
    u64 frame;

    if (!inf || !(inf->bars[0] & PCI_BAR_FLAG_IO))
        return -1;

    bar0 = inf->bars[0] & ~PCNET_IOPORT_MASK;
    g_pcnet.port  = (u16)bar0;
    g_pcnet.bus   = inf->bus;
    g_pcnet.pci_dev = inf->dev;
    g_pcnet.func  = inf->func;

    /* 8 KiB DMA block: init block + two descriptors + both buffers. */
    if (phys_alloc_block(1, &frame)) {
        pr_warn("pcnet: no DMA memory\n");
        return -1;
    }
    g_pcnet.dma_phys = (u32)(frame << PAGE_SHIFT);
    if (g_pcnet.dma_phys < 0x1000u ||
        (u64)g_pcnet.dma_phys + sizeof(struct pcnet_dma) > 0x100000000ull) {
        pr_warn("pcnet: DMA block outside 32-bit range\n");
        phys_free_block(frame, 1);
        return -1;
    }
    g_pcnet.dma = (struct pcnet_dma *)phys_to_virt(g_pcnet.dma_phys);

    /* Enable I/O (and bus mastering for the NIC's DMA). */
    cmd = pci_config_read16(inf->bus, inf->dev, inf->func, PCI_REG_COMMAND);
    pci_config_write16(inf->bus, inf->dev, inf->func, PCI_REG_COMMAND,
                       (u16)(cmd | PCI_CMD_IO_ENABLE | PCI_CMD_MEM_ENABLE |
                             PCI_CMD_MASTER_ENABLE));

    /* Use the interrupt line the firmware assigned, else the default. */
    line = pci_config_read8(inf->bus, inf->dev, inf->func,
                            PCI_REG_INT_LINE);
    g_pcnet.irq = (line && line != 0xFFu) ? line : X86_IRQ_NET;

    g_pcnet.present = 1;
    x86_irq_register(g_pcnet.irq, pcnet_irq_handler, 0);
    x64_pic_set_mask(g_pcnet.irq, 0);

    pr_info("pcnet: %02u:%02u.%u I/O 0x%04x IRQ %d DMA 0x%08x bound\n",
            (unsigned)inf->bus, (unsigned)inf->dev, (unsigned)inf->func,
            (unsigned)bar0, g_pcnet.irq, g_pcnet.dma_phys);

    pcnet_hw_init();
    return 0;
}

static struct driver g_pcnet_driver = {
    .name     = "pcnet",
    .id_table = pcnet_ids,
    .probe    = pcnet_probe,
};

void pcnet_driver_register(void)
{
    driver_register(&g_pcnet_driver);
}