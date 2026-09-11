#include <fs/vfs.h>
#include <core/core_printk.h>
#include <fs/tmpfs.h>
#include <iru_string.h>

/* initramfs: the boot-time root filesystem. In this phase the kernel
 * populates a tmpfs-backed root from built-in definitions (including the
 * embedded /bin/hello + /init images). Later this module parses a cpio
 * archive passed by the boot loader as a multiboot module. */

#define DEFAULT_ROOT_DIRS \
    { "dev", "proc", "sys", "tmp", "etc", "bin", "sbin", \
      "lib", "usr", "home", "root", "run", "opt", "var", "mnt" }

static const char *root_dirs[] = DEFAULT_ROOT_DIRS;

/* Embedded userland binaries (linked by the build system from
 * userland/programs/hello). Symbols rename to _binary_hello_elf_*. */
extern const u8 _binary_hello_elf_start[];
extern const u8 _binary_hello_elf_end[];

static void install_blob(const char *path, const u8 *data, size_t len,
                         struct vfs_inode *parent)
{
    const char *name = strrchr(path, '/');
    name = (name && name[1]) ? name + 1 : path;

    struct vfs_inode *node = tmpfs_create_node(parent, name, VFS_MODE_REG | 0755);
    struct vfs_file *f = vfs_open_inode(node, VFS_O_READ | VFS_O_WRITE);
    if (f) {
        vfs_write(f, data, len);
        vfs_close(f);
    }
}

void initramfs_init(void)
{
    for (size_t i = 0; i < sizeof(root_dirs) / sizeof(root_dirs[0]); i++) {
        char path[128];
        strcpy(path, "/");
        strcat(path, root_dirs[i]);
        vfs_mkdir(path);
    }

    /* A welcome file so userspace has something to read. */
    struct vfs_inode *etc = vfs_lookup("/etc");
    if (etc)
        tmpfs_create_node(etc, "version", VFS_MODE_REG | 0644);

    struct vfs_file *f = vfs_open("/etc/version", VFS_O_READ | VFS_O_WRITE);
    if (f) {
        static const char banner[] = "NEWOS 0.2.0-pre-alpha\n";
        vfs_write(f, banner, sizeof(banner) - 1);
        vfs_close(f);
    }

    /* /bin/hello (the phase-2 user program) and /init -> hello copy. */
    size_t hello_len = (size_t)(_binary_hello_elf_end - _binary_hello_elf_start);
    if (hello_len) {
        struct vfs_inode *bin = vfs_lookup("/bin");
        struct vfs_inode *root = vfs_root();
        if (bin)
            install_blob("/bin/hello", _binary_hello_elf_start, hello_len, bin);
        if (root)
            install_blob("/init", _binary_hello_elf_start, hello_len, root);
        pr_info("initramfs: installed /bin/hello + /init (%zu bytes)\n",
                hello_len);
    }

    pr_info("initramfs: built-in root populated\n");
}