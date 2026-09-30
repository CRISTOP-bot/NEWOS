#ifndef PROCFS_H
#define PROCFS_H

#include <fs/vfs.h>

struct vfs_inode *procfs_mount(struct vfs_inode *parent);

#endif
