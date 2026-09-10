#include <kernel/vfs.h>
#include <kernel/kmalloc.h>
#include <kernel/printk.h>
#include <libk/string.h>

/* Inode allocation and lifecycle. All inodes are owned by the VFS core;
 * registerd filesystems attach their data through `private`. */

static u64 g_next_ino = 1;

struct vfs_inode *vfs_alloc_inode(struct vfs_inode *parent, const char *name,
                                  u32 mode, struct inode_ops *ops,
                                  void *private)
{
    struct vfs_inode *in = kzalloc(sizeof(*in));
    if (!in)
        return NULL;

    in->ino = g_next_ino++;
    in->mode = mode;
    in->ops = ops;
    in->private = private;
    in->size = 0;
    in->parent = parent;
    list_init(&in->children);
    list_init(&in->chain);
    strncpy(in->name, name, sizeof(in->name) - 1);

    if (parent) {
        list_push_back(&parent->children, &in->chain);
        parent->size++;
    }

    return in;
}