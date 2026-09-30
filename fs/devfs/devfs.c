#include <fs/vfs.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>
#include <drivers/serial_16550.h>
#include <drivers/tty_console.h>
#include <drivers/ps2.h>
#include <iru_string.h>

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

/* /dev/serial is the userland console: writes go through the console
 * multiplexer so user programs (the /init shell, /bin/hello, ...) appear
 * on every registered backend (serial + VGA text), exactly like printk.
 * Reads stay on the UART: COM1 RX is the only input source (there is no
 * keyboard driver yet), so typing happens over serial. */
static int serial_write_dev(struct vfs_file *f, const void *buf, size_t len,
                            size_t *wrote)
{
    (void)f;
    console_write((const char *)buf, len);
    *wrote = len;
    return 0;
}

static struct devfs_device dev_null = {
    .name = "null", .read = null_read, .write = null_write,
};

/* /dev/zero: an endless stream of zeroes (real semantics, backed by
 * nothing). Shared read/write helpers keep the device table small. */
static int zero_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    (void)f;
    memset(buf, 0, len);
    *got = len;
    return 0;
}

static struct devfs_device dev_zero = {
    .name = "zero", .read = zero_read, .write = null_write,
};

/* /dev/console: the multiplexed console. Writes reach every registered
 * backend (serial + VGA text), reads are refused (input lives on stdin,
 * which merges serial + keyboard). */
static int console_write_dev(struct vfs_file *f, const void *buf, size_t len,
                             size_t *wrote)
{
    (void)f;
    console_write((const char *)buf, len);
    *wrote = len;
    return 0;
}

static struct devfs_device dev_console = {
    .name = "console", .read = NULL, .write = console_write_dev,
};

/* /dev/mouse: one "X <x> Y <y> B <buttons> W <wheel>\n" line per pending
 * pixel event. Reads drain the event ring and return 0 bytes when empty
 * (so cat terminates instead of spinning). */
static void putdec(char **p, int v)
{
    char tmp[12];
    int i = 0;
    if (v == 0) {
        *(*p)++ = '0';
        return;
    }
    if (v < 0) {
        *(*p)++ = '-';
        v = -v;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        *(*p)++ = tmp[--i];
}

static int mouse_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    char out[256];
    char *p = out;
    int x, y, b, w;

    (void)f;
    while ((size_t)(p - out) + 40 <= sizeof(out) &&
           ps2_mouse_pop_event(&x, &y, &b, &w)) {
        *p++ = 'X'; *p++ = ' ';
        putdec(&p, x);
        *p++ = ' '; *p++ = 'Y'; *p++ = ' ';
        putdec(&p, y);
        *p++ = ' '; *p++ = 'B'; *p++ = ' ';
        putdec(&p, b);
        *p++ = ' '; *p++ = 'W'; *p++ = ' ';
        putdec(&p, w);
        *p++ = '\n';
    }

    size_t n = (size_t)(p - out);
    if (n > len)
        n = len;
    memcpy(buf, out, n);
    *got = n;
    return 0;
}

static struct devfs_device dev_mouse = {
    .name = "mouse", .read = mouse_read, .write = null_write,
};

static struct devfs_device dev_serial = {
    .name = "serial",
    .read  = serial_read_dev,
    .write = serial_write_dev,
};

int devfs_is_serial(const struct vfs_file *f)
{
    return f && f->inode && f->inode->ops == &devfs_ops &&
           f->inode->private == &dev_serial;
}

static void devfs_register(struct devfs_device *dev)
{
    struct vfs_inode *devdir = vfs_lookup("/dev");
    if (!devdir)
        return;
    vfs_alloc_inode(devdir, dev->name, VFS_MODE_CHAR | 0644,
                    &devfs_ops, dev);
}

void devfs_init(void)
{
    vfs_mkdir("/dev");
    devfs_register(&dev_null);
    devfs_register(&dev_zero);
    devfs_register(&dev_serial);
    devfs_register(&dev_console);
    devfs_register(&dev_mouse);
    pr_info("devfs: mounted /dev (null, zero, serial, console, mouse)\n");
}