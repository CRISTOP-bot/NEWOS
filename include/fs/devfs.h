#ifndef FS_DEVFS_H
#define FS_DEVFS_H

void devfs_init(void);

/* 1 when the open file is /dev/serial (the merged console stdin). */
struct vfs_file;
int  devfs_is_serial(const struct vfs_file *f);

#endif