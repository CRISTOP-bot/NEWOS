#ifndef FS_TMPFS_H
#define FS_TMPFS_H

#include <kernel/types.h>
#include <kernel/vfs.h>

/* tmpfs public API. */
void tmpfs_init(void);
struct vfs_inode *tmpfs_create_node(struct vfs_inode *parent,
                                    const char *name, u32 mode);

#endif