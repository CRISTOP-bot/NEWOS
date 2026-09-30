#include <fs/vfs.h>
#include <fs/procfs/procfs.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>

static int procfs_read(struct vfs_file *f, void *buf, size_t len, size_t *got) {
    *got = 0;
    return 0;
}

static int procfs_readdir(struct vfs_inode *dir, void *buf, size_t len, size_t *got) {
    *got = 0;
    return 0;
}

static struct inode_ops procfs_root_ops = {
    .read = procfs_read,
    .readdir = procfs_readdir,
};

struct vfs_inode *procfs_mount(struct vfs_inode *parent) {
    struct vfs_inode *root = vfs_alloc_inode(parent, "proc", 0x4000 | 0755, &procfs_root_ops, NULL);
    if (!root) return NULL;
    return root;
}
