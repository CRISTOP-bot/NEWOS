#ifndef KERNEL_DRIVER_H
#define KERNEL_DRIVER_H

#include <kernel/types.h>
#include <libk/list.h>

/* Generic device model.
 *
 *   device  -> bound to -> driver
 *      |                     |
 *   belongs to a bus         owns a device class / subsystem
 *
 * Drivers register against buses; the bus layer matches drivers to
 * discovered devices and calls probe(). */

struct device;
struct bus;
struct driver;

struct device {
    u64 id;
    const char *name;
    struct bus *bus;
    struct driver *driver;
    struct list_node chain;   /* bus device list       */
    struct list_node list;    /* global device list    */
    void *private;
};

struct driver {
    const char *name;
    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);
    struct list_node list;    /* global driver list    */
};

struct bus {
    const char *name;
    struct list_node devices;
    struct list_node list;    /* global bus list       */
};

void device_model_init(void);

int  device_register(struct device *dev);
int  device_unregister(struct device *dev);
int  driver_register(struct driver *drv);
int  driver_unregister(struct driver *drv);
int  bus_register(struct bus *bus);

void device_model_dump(void);

/* Match & binding helpers used by bus implementations (PCI, etc.). */
int  driver_match_device(struct driver *drv, struct device *dev);
int  device_bind(struct device *dev, struct driver *drv);

/* Allocation helpers */
struct device *device_alloc(const char *name, struct bus *bus);
void           device_release(struct device *dev);

#endif