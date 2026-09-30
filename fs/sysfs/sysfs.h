#ifndef SYSFS_H
#define SYSFS_H

#include <fs/vfs.h>

struct vfs_inode *sysfs_mount(struct vfs_inode *parent);

#endif
