#include <drivers/drv_core.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* Global registration lists for the device model. */

static struct list_node g_bus_list = LIST_INIT(g_bus_list);
static struct list_node g_driver_list = LIST_INIT(g_driver_list);
static struct list_node g_device_list = LIST_INIT(g_device_list);

void device_model_init(void)
{
    pr_info("device model: core initialized\n");
}


int bus_register(struct bus *bus)
{
    if (!bus)
        return -1;
    list_init(&bus->devices);
    list_push_back(&g_bus_list, &bus->list);
    return 0;
}

int device_register(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;

    list_push_back(&dev->bus->devices, &dev->chain);
    list_push_back(&g_device_list, &dev->list);

    /* Try each driver registered for this bus. */
    struct list_node *node, *tmp;
    LIST_FOR_EACH_SAFE(node, tmp, &g_driver_list) {
        struct driver *drv = LIST_NODE_ENTRY(node, struct driver, list);
        if (drv->id_table) {
            /* ID-table drivers match any device with compatible IDs. */
            if (driver_match_device(drv, dev)) {
                device_bind(dev, drv);
                break;
            }
        } else if (dev->bus->name && drv->name &&
                   strstr(drv->name, dev->bus->name)) {
            if (driver_match_device(drv, dev)) {
                device_bind(dev, drv);
                break;
            }
        }
    }

    return 0;
}

int device_unregister(struct device *dev)
{
    if (!dev)
        return -1;
    list_remove(&dev->chain);
    list_remove(&dev->list);
    return 0;
}

int driver_register(struct driver *drv)
{
    if (!drv)
        return -1;
    list_push_back(&g_driver_list, &drv->list);
    return 0;
}

int driver_unregister(struct driver *drv)
{
    if (!drv)
        return -1;
    list_remove(&drv->list);
    return 0;
}

int driver_match_device(struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return 0;

    /* Hardware ID table match (PCI-style vendor/device/class). */
    if (drv->id_table) {
        for (const struct device_id *id = drv->id_table;
             id->vendor || id->device || id->class; id++) {
            if ((id->vendor == DEVICE_ID_ANY || id->vendor == dev->vendor) &&
                (id->device == DEVICE_ID_ANY || id->device == dev->device) &&
                (id->class   == 0u            || id->class   == dev->class))
                return 1;
        }
        return 0;
    }

    /* Legacy name match. */
    if (!drv->name || !dev->name)
        return 0;
    return strstr(drv->name, dev->name) != NULL ||
           strstr(dev->name, drv->name) != NULL;
}

int device_bind(struct device *dev, struct driver *drv)
{
    dev->driver = drv;
    if (drv->probe)
        drv->probe(dev);
    return 0;
}

void device_model_dump(void)
{
    printk("device model:\n");

    struct list_node *node;
    LIST_FOR_EACH(node, &g_bus_list) {
        struct bus *bus = LIST_NODE_ENTRY(node, struct bus, list);
        printk("  bus   %s\n", bus->name);
    }
    LIST_FOR_EACH(node, &g_driver_list) {
        struct driver *drv = LIST_NODE_ENTRY(node, struct driver, list);
        printk("  drv   %s\n", drv->name);
    }
    LIST_FOR_EACH(node, &g_device_list) {
        struct device *dev = LIST_NODE_ENTRY(node, struct device, list);
        printk("  dev   %s @ %s\n", dev->name,
               dev->bus ? dev->bus->name : "?");
    }
}