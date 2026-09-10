#include <kernel/vfs.h>
#include <kernel/kmalloc.h>
#include <kernel/kernel.h>
#include <kernel/printk.h>
#include <fs/tmpfs/tmpfs.h>
#include <libk/string.h>

/* tmpfs: an in-memory file system backed by the kernel heap. */

struct tmpfs_data {
    u8 *data;
    size_t cap;
};

static int tmpfs_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    struct tmpfs_data *d = (struct tmpfs_data *)f->inode->private;
    if (!d || f->offset >= f->inode->size) {
        *got = 0;
        return 0;
    }
    size_t avail = f->inode->size - f->offset;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf, d->data + f->offset, n);
    *got = n;
    return 0;
}

static int tmpfs_write(struct vfs_file *f, const void *buf, size_t len,
                       size_t *wrote)
{
    struct tmpfs_data *d = (struct tmpfs_data *)f->inode->private;

    if (!d) {
        d = kzalloc(sizeof(*d));
        if (!d)
            return -1;
        f->inode->private = d;
    }

    size_t end = f->offset + len;
    if (end > d->cap) {
        size_t new_cap = MAX(d->cap * 2, 64);
        while (new_cap < end)
            new_cap *= 2;
        u8 *new_data = kmalloc(new_cap);
        if (!new_data)
            return -1;
        if (d->data) {
            memcpy(new_data, d->data, d->cap);
            kfree(d->data);
        }
        d->data = new_data;
        d->cap = new_cap;
    }

    memcpy(d->data + f->offset, buf, len);
    f->inode->size = MAX(f->inode->size, end);
    *wrote = len;
    return 0;
}

static int tmpfs_truncate(struct vfs_inode *inode, size_t size)
{
    struct tmpfs_data *d = (struct tmpfs_data *)inode->private;
    if (d && size > d->cap)
        return -1;
    inode->size = size;
    return 0;
}

static struct inode_ops tmpfs_ops = {
    .read     = tmpfs_read,
    .write    = tmpfs_write,
    .readdir  = NULL,
    .truncate = tmpfs_truncate,
};

struct vfs_inode *tmpfs_create_node(struct vfs_inode *parent,
                                    const char *name, u32 mode)
{
    return vfs_alloc_inode(parent, name, mode | 0644, &tmpfs_ops, NULL);
}

void tmpfs_init(void)
{
    pr_info("tmpfs: mounted\n");
}