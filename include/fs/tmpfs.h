#ifndef FS_TMPFS_H
#define FS_TMPFS_H

#include <core/core_types.h>
#include <fs/vfs.h>

/* tmpfs public API. */
void tmpfs_init(void);
struct vfs_inode *tmpfs_create_node(struct vfs_inode *parent,
                                    const char *name, u32 mode);

#endif