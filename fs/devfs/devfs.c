#include <kernel/vfs.h>
#include <kernel/kmalloc.h>
#include <kernel/printk.h>
#include <drivers/serial/serial.h>
#include <libk/string.h>

/* devfs: device file system. Each entry is an inode whose read/write calls
 * callbacks bound to a kernel driver. */

struct devfs_device {
    const char *name;
    int (*read)(struct vfs_file *f, void *buf, size_t len, size_t *got);
    int (*write)(struct vfs_file *f, const void *buf, size_t len,
                 size_t *wrote);
};

static int devfs_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    struct devfs_device *dev = (struct devfs_device *)f->inode->private;
    if (!dev || !dev->read) {
        *got = 0;
        return -1;
    }
    return dev->read(f, buf, len, got);
}

static int devfs_write(struct vfs_file *f, const void *buf, size_t len,
                       size_t *wrote)
{
    struct devfs_device *dev = (struct devfs_device *)f->inode->private;
    if (!dev || !dev->write) {
        *wrote = 0;
        return -1;
    }
    return dev->write(f, buf, len, wrote);
}

static struct inode_ops devfs_ops = {
    .read     = devfs_read,
    .write    = devfs_write,
};

/* --- device backends ----------------------------------------------------- */

static int null_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    (void)f; (void)buf; (void)len;
    *got = 0;
    return 0;
}

static int null_write(struct vfs_file *f, const void *buf, size_t len,
                      size_t *wrote)
{
    (void)f; (void)buf;
    *wrote = len;
    return 0;
}

static int serial_read_dev(struct vfs_file *f, void *buf, size_t len,
                           size_t *got)
{
    (void)f;
    size_t n = 0;
    for (; n < len; n++) {
        int c = serial_getc(SERIAL_COM1);
        if (c < 0)
            break;
        ((u8 *)buf)[n] = (u8)c;
    }
    *got = n;
    return 0;
}

static int serial_write_dev(struct vfs_file *f, const void *buf, size_t len,
                            size_t *wrote)
{
    (void)f;
    serial_write(SERIAL_COM1, buf, len);
    *wrote = len;
    return 0;
}

static struct devfs_device dev_null = {
    .name = "null", .read = null_read, .write = null_write,
};

static struct devfs_device dev_serial = {
    .name = "serial",
    .read  = serial_read_dev,
    .write = serial_write_dev,
};

static void devfs_register(struct devfs_device *dev)
{
    struct vfs_inode *devdir = vfs_lookup("/dev");
    if (!devdir)
        return;
    vfs_alloc_inode(devdir, dev->name, VFS_MODE_CHAR | 0600,
                    &devfs_ops, dev);
}

void devfs_init(void)
{
    vfs_mkdir("/dev");
    devfs_register(&dev_null);
    devfs_register(&dev_serial);
    pr_info("devfs: mounted /dev (null, serial)\n");
}