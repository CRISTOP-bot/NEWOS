#include <drivers/pci.h>
#include <core/core.h>

/* Human-readable names for common PCI vendor/device IDs. The AMD (0x1022)
 * and Intel (0x8086) tables cover the chipset devices this os is most
 * likely to meet on bare metal; a handful of other prominent vendors are
 * listed for readability in QEMU. Lookups return NULL when unknown, and the
 * enumerator falls back to the raw hex ids. */

struct pci_id_name {
    u16   id;
    const char *name;
};

/* --- Vendors ------------------------------------------------------------ */

static const struct pci_id_name vendor_table[] = {
    { 0x1022, "AMD" },
    { 0x8086, "Intel" },
    { 0x1028, "Dell" },
    { 0x1033, "NEC" },
    { 0x10DE, "NVIDIA" },
    { 0x10EC, "Realtek" },
    { 0x11AD, "Realtek (audio)" },
    { 0x14E4, "Broadcom" },
    { 0x15AD, "VMware" },
    { 0x1002, "ATI" },
    { 0x1234, "Bochs/QEMU" },
    { 0x1AF4, "Red Hat / VirtIO" },
    { 0x1B36, "Red Hat" },
    { 0x106B, "Apple" },
    { 0x1414, "Microsoft" },
    { 0x80EE, "VirtualBox" },
};

/* --- Intel (0x8086) ----------------------------------------------------- */

static const struct pci_id_name intel_table[] = {
    { 0x1237, "440FX/82441FX Host Bridge" },
    { 0x7000, "PIIX3 ISA Bridge" },
    { 0x7010, "PIIX3 IDE Controller" },
    { 0x7110, "PIIX4 ISA Bridge" },
    { 0x7111, "PIIX4 IDE Controller" },
    { 0x7112, "PIIX4 USB Controller" },
    { 0x7113, "PIIX4 ACPI/PM Controller" },
    { 0x2916, "ICH9/ICH7 LPC Interface" },
    { 0x2918, "ICH9/ICH7 LPC Interface" },
    { 0x2930, "ICH9 SMBus Controller" },
    { 0x2922, "ICH9 SATA Controller (AHCI)" },
    { 0x2926, "ICH9 SATA Controller (IDE)" },
    { 0x29C0, "Q35 Host Bridge" },
    { 0x29C1, "Q35 PCI Express Root Port" },
    { 0x100E, "82540EM Gigabit Ethernet" },
    { 0x101D, "82546EB Gigabit Ethernet" },
    { 0x10D3, "82574L Gigabit Ethernet" },
    { 0x105E, "82571EB Gigabit Ethernet" },
    { 0x109A, "82573L Gigabit Ethernet" },
    { 0x2448, "82801BA Hub Interface" },
    { 0x2415, "82801AA AC'97 Audio" },
};

/* --- AMD (0x1022) -------------------------------------------------------- */

static const struct pci_id_name amd_table[] = {
    { 0x1450, "170h Root Complex / Northbridge" },
    { 0x1453, "170h Data Fabric" },
    { 0x146A, "170h PCIe Root Port" },
    { 0x15D0, "16h I/O MMU (IOMMUv2)" },
    { 0x15D1, "16h IOMMU" },
    { 0x1455, "17h IOMMU" },
    { 0x1481, "Family 19h IOMMU" },
    { 0x7800, "FCH SMBus Controller" },
    { 0x7801, "FCH LPC Bridge" },
    { 0x7802, "FCH AZAP MMB" },
    { 0x7807, "FCH IDE Controller" },
    { 0x7808, "FCH Audio (Azalia)" },
    { 0x7809, "FCH USB EHCI Controller" },
    { 0x7814, "FCH USB XHCI Controller" },
    { 0x7900, "Zeppelin/19h FCH SMBus" },
    { 0x790B, "FCH SMBus Controller" },
    { 0x790E, "FCH LPC Bridge" },
    { 0x7936, "FCH USB EHCI Controller" },
    { 0x5A51, "FCH SATA Controller (AHCI)" },
    { 0x7901, "FCH SATA Controller (AHCI)" },
    { 0x43D5, "X370/X470 SATA Controller (AHCI)" },
    { 0x2000, "PCnet-PCI II (Am79C970A) Ethernet" },
};

/* --- Other vendors (a handful) ------------------------------------------- */

static const struct pci_id_name misc_table[] = {
    /* Bochs/QEMU */
    { 0x1111, "VGA Compatible Controller" },
    /* VirtIO (Red Hat) */
    { 0x1000, "VirtIO Network Device" },
    { 0x1001, "VirtIO Block Device" },
    { 0x1002, "VirtIO Memory Balloon" },
    { 0x1004, "VirtIO Scsi Controller" },
    { 0x1005, "VirtIO Console" },
    { 0x1009, "VirtIO Network Device" },
    /* VMware */
    { 0x0405, "SVGA II Adapter" },
    { 0x0720, "VMware Communication Interface" },
    /* NVIDIA */
    { 0x00F0, "PCI VGA Compatible Controller" },
    /* Realtek */
    { 0x8139, "RTL-8139 10/100 Ethernet" },
    { 0x8168, "RTL-8168 Gigabit Ethernet" },
    /* VirtualBox */
    { 0xBEAD, "VirtualBox Graphics Adapter (VBoxVGA)" },
    { 0xCAFE, "VirtualBox Guest Service (VBoxGuest)" },
    { 0x2000, "PCnet-PCI II Ethernet" },
};

static const char *lookup(const struct pci_id_name *table, unsigned n,
                          u16 id)
{
    for (unsigned i = 0; i < n; i++)
        if (table[i].id == id)
            return table[i].name;
    return NULL;
}

const char *pci_vendor_name(u16 vendor)
{
    return lookup(vendor_table, ARRAY_SIZE(vendor_table), vendor);
}

const char *pci_device_name(u16 vendor, u16 device)
{
    const struct pci_id_name *table = NULL;
    unsigned n = 0;

    if (vendor == 0x1022) {
        table = amd_table;
        n = sizeof(amd_table) / sizeof(amd_table[0]);
    } else if (vendor == 0x8086) {
        table = intel_table;
        n = sizeof(intel_table) / sizeof(intel_table[0]);
    }

    if (table) {
        const char *name = lookup(table, n, device);
        if (name)
            return name;
    }

    return lookup(misc_table, sizeof(misc_table) / sizeof(misc_table[0]),
                  device);
}

const char *pci_class_name(u8 base, u8 sub)
{
    switch (base) {
    case PCI_CLASS_MASS_STORAGE:
        switch (sub) {
        case 0x00: return "mass storage (unspecified)";
        case PCI_CLASS_STORAGE_IDE: return "IDE controller";
        case 0x04: return "RAID controller";
        case PCI_CLASS_STORAGE_SATA: return "SATA controller";
        case 0x85: return "ATA controller";
        default:   return "mass storage";
        }
    case PCI_CLASS_NETWORK:
        switch (sub) {
        case 0x00: return "network (ethernet)";
        case 0x01: return "network (token ring)";
        case 0x80: return "network (other)";
        default:   return "network";
        }
    case PCI_CLASS_DISPLAY:
        switch (sub) {
        case 0x00: return "VGA compatible";
        case 0x02: return "3D display";
        default:   return "display";
        }
    case PCI_CLASS_BRIDGE:
        switch (sub) {
        case PCI_CLASS_BRIDGE_HOST: return "host bridge";
        case PCI_CLASS_BRIDGE_ISA:  return "ISA bridge";
        case PCI_CLASS_BRIDGE_PCI:  return "PCI-to-PCI bridge";
        case 0x7: return "CardBus bridge";
        default:  return "bridge";
        }
    case PCI_CLASS_SERIAL:
        switch (sub) {
        case PCI_CLASS_SERIAL_USB:   return "USB controller";
        case PCI_CLASS_SERIAL_SMBUS: return "SMBus controller";
        case 0x07: return "serial port controller";
        default:   return "serial bus";
        }
    case PCI_CLASS_SYSTEM:
        switch (sub) {
        case 0x00: return "system (PIC)";
        case 0x03: return "system (timer)";
        case 0x05: return "system (SMBus)";
        case 0x0C: return "system (IOMMU)";
        case 0x01: return "system (DMA)";
        default:   return "system peripheral";
        }
    default:
        return NULL;
    }
}