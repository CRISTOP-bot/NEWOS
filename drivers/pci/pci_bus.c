#include <drivers/pci.h>
#include <drivers/drv_core.h>
#include <drivers/pcnet.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>
#include <iru_string.h>
#include <x86_io.h>

/* PCI bus driver: config-space access over the legacy 0xCF8/0xCFC ports,
 * bus-0 enumeration with recursion through PCI-to-PCI bridges, and
 * registration of every function with the device model ("pci" bus). */

static struct bus g_pci_bus = { .name = "pci" };
static u8 g_bus_visited[256];
static unsigned g_pci_count;

void pci_chipset_driver_register(void);

/* ------------------------------------------------------------------ *
 *  Config-space access
 * ------------------------------------------------------------------ */

static u32 pci_config_addr(u8 bus, u8 dev, u8 func, u8 off)
{
    return PCI_CONFIG_ENABLE
         | ((u32)bus << 16)
         | ((u32)dev << 11)
         | ((u32)func << 8)
         | ((u32)off & 0xFCu);
}

u32 pci_config_read32(u8 bus, u8 dev, u8 func, u8 off)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    return inl(PCI_CONFIG_DATA_PORT);
}

u16 pci_config_read16(u8 bus, u8 dev, u8 func, u8 off)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    return inw(PCI_CONFIG_DATA_PORT + (off & 2u));
}

u8 pci_config_read8(u8 bus, u8 dev, u8 func, u8 off)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    return inb(PCI_CONFIG_DATA_PORT + (off & 3u));
}

void pci_config_write32(u8 bus, u8 dev, u8 func, u8 off, u32 val)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA_PORT, val);
}

void pci_config_write16(u8 bus, u8 dev, u8 func, u8 off, u16 val)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    outw(PCI_CONFIG_DATA_PORT + (off & 2u), val);
}

void pci_config_write8(u8 bus, u8 dev, u8 func, u8 off, u8 val)
{
    outl(PCI_CONFIG_ADDR_PORT, pci_config_addr(bus, dev, func, off));
    outb(PCI_CONFIG_DATA_PORT + (off & 3u), val);
}

/* ------------------------------------------------------------------ *
 *  Naming + registration
 * ------------------------------------------------------------------ */

static void pci_hex16(char *buf, u16 v)
{
    static const char hexd[] = "0123456789ABCDEF";
    buf[0] = hexd[(v >> 12) & 0xF];
    buf[1] = hexd[(v >> 8) & 0xF];
    buf[2] = hexd[(v >> 4) & 0xF];
    buf[3] = hexd[v & 0xF];
    buf[4] = '\0';
}

static void pci_build_name(struct pci_dev_info *inf)
{
    const char *vn = pci_vendor_name(inf->vendor);
    const char *dn = pci_device_name(inf->vendor, inf->device);

    if (vn && dn) {
        strcpy(inf->name, vn);
        strcat(inf->name, " ");
        strcat(inf->name, dn);
    } else if (dn) {
        strcpy(inf->name, dn);
    } else {
        char id[5];
        pci_hex16(id, inf->vendor);
        strcpy(inf->name, id);
        strcat(inf->name, ":");
        pci_hex16(id, inf->device);
        strcat(inf->name, id);
    }
}

static void pci_register_device(struct pci_dev_info *inf)
{
    pci_build_name(inf);

    struct device *dev = device_alloc(inf->name, &g_pci_bus);
    if (!dev) {
        pr_warn("PCI: no memory for %02u:%02u.%u\n",
                (unsigned)inf->bus, (unsigned)inf->dev, (unsigned)inf->func);
        return;
    }

    dev->vendor  = inf->vendor;
    dev->device  = inf->device;
    dev->class   = PCI_MAKE_CLASS(inf->base_class, inf->sub_class,
                                  inf->prog_if);
    dev->private = inf;

    device_register(dev);
    g_pci_count++;
}

/* ------------------------------------------------------------------ *
 *  Enumeration
 * ------------------------------------------------------------------ */

static void pci_scan_bus(u8 bus, unsigned depth)
{
    if (depth >= 4 || g_bus_visited[bus])
        return;
    g_bus_visited[bus] = 1;

    for (u8 dev = 0; dev < PCI_SLOT_MAX; dev++) {
        u8 header = pci_config_read8(bus, dev, 0, PCI_REG_HEADER);
        u16 vendor = pci_config_read16(bus, dev, 0, PCI_REG_VENDOR);
        if (vendor == PCI_VENDOR_INVALID || vendor == PCI_DEVICE_NONE)
            continue;

        u8 nfunc = (header & PCI_HEADER_MULTIFUNC) ? PCI_FUNC_MAX : 1u;

        for (u8 func = 0; func < nfunc; func++) {
            vendor = pci_config_read16(bus, dev, func, PCI_REG_VENDOR);
            if (vendor == PCI_VENDOR_INVALID || vendor == PCI_DEVICE_NONE)
                continue;

            /* Header type must be read per-function: bridges exposing
             * multifunction backends report a different header on func!=0,
             * and reusing func0's value misclassifies them. */
            u8 fn_header = pci_config_read8(bus, dev, func, PCI_REG_HEADER);

            struct pci_dev_info *inf = kzalloc(sizeof(*inf));
            if (!inf) {
                pr_warn("PCI: no memory (bus %u dev %u func %u)\n",
                        (unsigned)bus, (unsigned)dev, (unsigned)func);
                return;
            }

            inf->bus  = bus;
            inf->dev  = dev;
            inf->func = func;
            inf->vendor = vendor;
            inf->device = pci_config_read16(bus, dev, func, PCI_REG_DEVICE);
            inf->base_class = pci_config_read8(bus, dev, func, PCI_REG_CLASS);
            inf->sub_class  = pci_config_read8(bus, dev, func,
                                               PCI_REG_SUBCLASS);
            inf->prog_if    = pci_config_read8(bus, dev, func,
                                               PCI_REG_PROG_IF);
            inf->header_type = fn_header & ~PCI_HEADER_MULTIFUNC;

            for (unsigned i = 0; i < 6; i++)
                inf->bars[i] = pci_config_read32(bus, dev, func,
                                                 PCI_REG_BAR0 + (u8)(4 * i));

            if (inf->base_class == PCI_CLASS_BRIDGE &&
                inf->header_type == PCI_HEADER_BRIDGE) {
                u8 sec = pci_config_read8(bus, dev, func, PCI_REG_SEC_BUS);
                if (sec != 0 && sec != bus && !g_bus_visited[sec])
                    pci_scan_bus(sec, depth + 1);
            }

            pci_register_device(inf);
        }
    }
}

unsigned pci_bus_scan(void)
{
    memset(g_bus_visited, 0, sizeof(g_bus_visited));
    g_pci_count = 0;
    pci_scan_bus(0, 0);
    return g_pci_count;
}

/* ------------------------------------------------------------------ *
 *  Boot entry
 * ------------------------------------------------------------------ */

void pci_init(void)
{
    bus_register(&g_pci_bus);
    pci_chipset_driver_register();
    pcnet_driver_register();

    unsigned n = pci_bus_scan();
    pr_info("PCI: %u function(s) found\n", n);

    /* Walk the bus devices and print the concise table. */
    struct list_node *node;
    LIST_FOR_EACH(node, &g_pci_bus.devices) {
        struct device *dev = LIST_NODE_ENTRY(node, struct device, chain);
        struct pci_dev_info *inf = dev->private;
        if (!inf)
            continue;
        const char *bound = (dev->driver && dev->driver->name)
                                ? dev->driver->name : "";
        printk("PCI: %02u:%02u.%u  %04x:%04x  %-28s %s%s%s\n",
               (unsigned)inf->bus, (unsigned)inf->dev, (unsigned)inf->func,
               inf->vendor, inf->device, inf->name,
               pci_class_name(inf->base_class, inf->sub_class),
               bound[0] ? " -> " : "", bound);
    }
}