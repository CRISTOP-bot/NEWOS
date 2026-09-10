#include <kernel/driver.h>
#include <kernel/kmalloc.h>
#include <libk/string.h>

/* Device lifecycle helpers. Buses create device objects with bus_register()
 * + device_register(); this unit provides the allocation helper and the
 * per-device class/type tags kept for the device tree. */

struct device *device_alloc(const char *name, struct bus *bus)
{
    struct device *dev = kzalloc(sizeof(*dev));
    if (!dev)
        return NULL;
    dev->name = name;
    dev->bus = bus;
    return dev;
}

void device_release(struct device *dev)
{
    device_unregister(dev);
    kfree(dev);
}