#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <core/core_types.h>
#include <iru_list.h>

/* Virtual File System.
 *
 * The VFS provides the uniform inode/file abstraction used by every file
 * system implementation (tmpfs, devfs, and later nativefs/FAT/ext4). Paths
 * are resolved in a single mount namespace whose root is set at vfs_init().
 */

#define VFS_MODE_TYPE_MASK  0170000
#define VFS_MODE_DIR        0040000
#define VFS_MODE_REG        0100000
#define VFS_MODE_CHAR       0020000
#define VFS_MODE_BLOCK      0060000
#define VFS_MODE_PERM(o)    ((o) & 07777)

#define VFS_O_READ   1
#define VFS_O_WRITE  2
#define VFS_O_CREATE 4

struct vfs_inode;
struct vfs_file;

struct inode_ops {
    int (*read)(struct vfs_file *f, void *buf, size_t len, size_t *got);
    int (*write)(struct vfs_file *f, const void *buf, size_t len,
                 size_t *wrote);
    int (*readdir)(struct vfs_inode *dir, void *buf, size_t len,
                   size_t *got);
    int (*truncate)(struct vfs_inode *inode, size_t size);
    void (*destroy)(struct vfs_inode *inode);   /* free fs-private data */
};

struct vfs_inode {
    u64 ino;
    u32 mode;                  /* type | permissions            */
    u64 size;
    struct vfs_inode *parent;  /* NULL for the root mount      */
    struct list_node children; /* directory listing (dirs)     */
    struct list_node chain;    /* links the inode into friends */
    struct inode_ops *ops;
    void *private;
    char name[64];
};

struct vfs_file {
    struct vfs_inode *inode;
    u64 offset;
    int flags;
    u32 uflags;                  /* Linux O_* bits as passed to open(2) */
};

void vfs_init(void);

struct vfs_inode *vfs_root(void);
struct vfs_inode *vfs_lookup(const char *path);
/* cwd-aware lookup: absolute paths ignore cwd, relative ones resolve
 * under it (cwd must be absolute, e.g. "/home/user"). */
struct vfs_inode *vfs_lookup_cwd(const char *cwd, const char *path);
int  vfs_mkdir(const char *path);
int  vfs_create(const char *path);
/* Remove a file or an empty directory. Refuses non-empty directories. */
int  vfs_unlink(const char *path);
/* Move oldpath onto newpath, replacing the destination. Both paths must be
 * absolute and resolve inside the in-memory tree (tmpfs, devfs, sysfs):
 * on-disk filesystems keep their own directory state and are refused. */
int  vfs_rename(const char *oldpath, const char *newpath);
struct vfs_file *vfs_open(const char *path, int flags);
struct vfs_file *vfs_open_inode(struct vfs_inode *inode, int flags);
int  vfs_read(struct vfs_file *f, void *buf, size_t len);
int  vfs_write(struct vfs_file *f, const void *buf, size_t len);
int  vfs_close(struct vfs_file *f);
int  vfs_readdir(struct vfs_inode *dir, char **names, size_t max, size_t *n);
void vfs_dump_tree(void);

/* Registration of a filesystem's directory/device node underneath `parent`.
 * Used by tmpfs (real dirs) and devfs (device files). */
struct vfs_inode *vfs_alloc_inode(struct vfs_inode *parent, const char *name,
                                  u32 mode, struct inode_ops *ops,
                                  void *private);

#endif