#ifndef DRIVER_PCI_H
#define DRIVER_PCI_H

#include <core/core_types.h>

/* Bus driver for the PCI hierarchy (config-space access + enumeration + the
 * "pci" bus in the device model).
 *
 * PCI devices from any vendor are discovered on the standard 0xCF8/0xCFC
 * configuration ports (no MMCONFIG/ECAM dependency), named from the ID
 * tables in pci_ids.c, and registered as device-model devices so that
 * drivers can bind to them by vendor/device/class (see struct device_id in
 * drv_core.h). */

#define PCI_CONFIG_ADDR_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu
#define PCI_CONFIG_ENABLE    0x80000000u

#define PCI_VENDOR_INVALID   0xFFFFu
#define PCI_DEVICE_NONE      0x0000u

#define PCI_SLOT_MAX         32u
#define PCI_FUNC_MAX         8u

/* Configuration-register offsets (in bytes). */
#define PCI_REG_VENDOR       0x00u   /* 16-bit */
#define PCI_REG_DEVICE       0x02u   /* 16-bit */
#define PCI_REG_COMMAND      0x04u   /* 16-bit */
#define PCI_REG_STATUS       0x06u   /* 16-bit */
#define PCI_REG_REVISION     0x08u   /*  8-bit */
#define PCI_REG_PROG_IF      0x09u   /*  8-bit */
#define PCI_REG_SUBCLASS     0x0Au   /*  8-bit */
#define PCI_REG_CLASS        0x0Bu   /*  8-bit */
#define PCI_REG_HEADER       0x0Eu   /*  8-bit */
#define PCI_REG_BAR0         0x10u
#define PCI_REG_BAR5         0x24u   /* BAR0 + 4*5   */
#define PCI_REG_SEC_BUS      0x19u   /* header type 1 (bridge) */
#define PCI_REG_SUB_BUS      0x1Au
#define PCI_REG_INT_LINE     0x3Cu   /* interrupt line (LIN) */

/* Header types (low 7 bits of PCI_REG_HEADER). */
#define PCI_HEADER_DEVICE    0x00u
#define PCI_HEADER_BRIDGE    0x01u
#define PCI_HEADER_CARDBUS   0x02u
#define PCI_HEADER_MULTIFUNC 0x80u   /* bit 7 */

/* BAR flags (low bits of a BAR value). */
#define PCI_BAR_FLAG_IO      0x01u
#define PCI_BAR_FLAG_64BIT   0x04u

/* Primary command register bits (PCI_REG_COMMAND). */
#define PCI_CMD_IO_ENABLE    0x0001u
#define PCI_CMD_MEM_ENABLE   0x0002u
#define PCI_CMD_MASTER_ENABLE 0x0004u

/* Class codes decoded from PCI_REG_CLASS / PCI_REG_SUBCLASS. */
#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_CLASS_NETWORK      0x02u
#define PCI_CLASS_DISPLAY      0x03u
#define PCI_CLASS_MULTIMEDIA   0x04u
#define PCI_CLASS_MEMORY       0x05u
#define PCI_CLASS_BRIDGE       0x06u
#define PCI_CLASS_SYSTEM       0x08u
#define PCI_CLASS_INPUT        0x09u
#define PCI_CLASS_SERIAL       0x0Cu

#define PCI_CLASS_BRIDGE_HOST  0x00u
#define PCI_CLASS_BRIDGE_ISA   0x01u
#define PCI_CLASS_BRIDGE_PCI   0x04u

#define PCI_CLASS_STORAGE_IDE  0x01u
#define PCI_CLASS_STORAGE_SATA 0x06u

#define PCI_CLASS_SERIAL_SMBUS 0x05u
#define PCI_CLASS_SERIAL_USB   0x03u

/* Build a PCI class code as consumed by struct device::class /
 * struct device_id::class. */
#define PCI_MAKE_CLASS(base, sub, prog) \
    (((u32)(base) << 16) | ((u32)(sub) << 8) | (u32)(prog))

/* Bus->PIC IRQ mapping for two-pin intel default routing (see pci bus). */
#define PCI_IRQ_LINE_BASE    11u   /* X86_IRQ_NET */

/* Configuration-space accessors. `off` need not be aligned; the low bits of
 * the port address select the byte lane within the 32-bit window. */
u8  pci_config_read8(u8 bus, u8 dev, u8 func, u8 off);
u16 pci_config_read16(u8 bus, u8 dev, u8 func, u8 off);
u32 pci_config_read32(u8 bus, u8 dev, u8 func, u8 off);
void pci_config_write8(u8 bus, u8 dev, u8 func, u8 off, u8 val);
void pci_config_write16(u8 bus, u8 dev, u8 func, u8 off, u16 val);
void pci_config_write32(u8 bus, u8 dev, u8 func, u8 off, u32 val);

/* Enumerates bus 0 and recurses through PCI-to-PCI bridges, registering each
 * function with the device model. Returns the number of functions found. */
unsigned pci_bus_scan(void);

/* One-time init: registers the "pci" bus, the chipset driver, then scans. */
void pci_init(void);

/* Name lookup from the AMD/Intel (and assorted) ID tables. */
const char *pci_vendor_name(u16 vendor);
const char *pci_device_name(u16 vendor, u16 device);
const char *pci_class_name(u8 base, u8 sub);

/* Per-function information captured while enumerating the bus and stored
 * as the device model's private data (dev->private) for bound drivers. */
struct pci_dev_info {
    u8  bus;
    u8  dev;
    u8  func;
    u16 vendor;
    u16 device;
    u8  base_class;
    u8  sub_class;
    u8  prog_if;
    u8  header_type;
    u32 bars[6];
    char name[64];
};

#endif