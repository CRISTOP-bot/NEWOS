#include <drivers/pci.h>
#include <drivers/drv_core.h>
#include <core/core_printk.h>

/* Chipset front-end driver. Binds (by vendor/device ID, not name) the
 * northbridge / LPC / IOMMU functions of both the Intel and AMD chipset
 * families and reports which platform we are running on. This is the proof
 * that the device model's ID table path works for both vendors. */

static const struct device_id chipset_ids[] = {
    /* Intel -- PIIX-era 440FX board. */
    { 0x8086, 0x1237, 0 },   /* 440FX/82441FX host bridge           */
    { 0x8086, 0x7000, 0 },   /* PIIX3 ISA bridge                     */
    { 0x8086, 0x7110, 0 },   /* PIIX4 ISA bridge                     */
    { 0x8086, 0x7113, 0 },   /* PIIX4 ACPI/PM controller             */

    /* Intel -- Q35 / ICH9 board. */
    { 0x8086, 0x29C0, 0 },   /* Q35 host bridge                      */
    { 0x8086, 0x2916, 0 },   /* ICH9 LPC interface                   */

    /* AMD -- 16h/17h/19h root complexes and FCH LPC. */
    { 0x1022, 0x15D0, 0 },   /* 16h root complex / data fabric       */
    { 0x1022, 0x1450, 0 },   /* 17h root complex                     */
    { 0x1022, 0x7801, 0 },   /* FCH LPC bridge (family 15h/16h)      */
    { 0x1022, 0x790E, 0 },   /* FCH LPC bridge (family 17h/19h)      */

    /* AMD -- IOMMU. */
    { 0x1022, 0x15D1, 0 },   /* 16h I/O MMU                          */
    { 0x1022, 0x1481, 0 },   /* 19h I/O MMU                          */

    { 0, 0, 0 },
};

static int chipset_probe(struct device *dev)
{
    if (dev->vendor == 0x8086)
        pr_info("chipset: Intel platform function '%s' bound\n",
                dev->name);
    else if (dev->vendor == 0x1022)
        pr_info("chipset: AMD platform function '%s' bound\n",
                dev->name);
    else
        pr_info("chipset: platform function '%s' bound\n", dev->name);
    return 0;
}

static struct driver g_chipset_driver = {
    .name     = "chipset",
    .id_table = chipset_ids,
    .probe    = chipset_probe,
};

void pci_chipset_driver_register(void)
{
    driver_register(&g_chipset_driver);
}