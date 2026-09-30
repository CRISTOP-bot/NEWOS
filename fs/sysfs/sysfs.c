#include <fs/vfs.h>
#include <fs/sysfs/sysfs.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>
#include <lib/kernel/iru_string.h>

static int sysfs_read(struct vfs_file *f, void *buf, size_t len, size_t *got) {
    *got = 0;
    return 0;
}

static int sysfs_readdir(struct vfs_inode *dir, void *buf, size_t len, size_t *got) {
    *got = 0;
    return 0;
}

static struct inode_ops sysfs_root_ops = {
    .read = sysfs_read,
    .readdir = sysfs_readdir,
};

struct vfs_inode *sysfs_mount(struct vfs_inode *parent) {
    struct vfs_inode *root = vfs_alloc_inode(parent, "sys", 0x4000 | 0755, &sysfs_root_ops, NULL);
    return root;
}
