#include <fs/vfs.h>
#include <core/core_printk.h>
#include <fs/tmpfs.h>
#include <iru_string.h>

/* initramfs: the boot-time root filesystem. In this phase the kernel
 * populates a tmpfs-backed root from built-in definitions (including the
 * embedded /bin/hello + /init images). Later this module parses a cpio
 * archive passed by the boot loader as a multiboot module. */

#define DEFAULT_ROOT_DIRS \
    { "boot", "dev", "proc", "sys", "tmp", "etc", "bin", "sbin", \
      "lib", "lib64", "usr", "home", "root", "run", "opt", "var", "mnt" }

static const char *root_dirs[] = DEFAULT_ROOT_DIRS;

/* Embedded userland binaries (linked by the build system). Symbol renames
 * to _binary_<name>_elf_*. */
extern const u8 _binary_hello_elf_start[];
extern const u8 _binary_hello_elf_end[];
extern const u8 _binary_init_elf_start[];
extern const u8 _binary_init_elf_end[];

/* Embedded test images for the img viewer (splash.bmp, test.ppm). */
extern const u8 _binary_splash_bmp_start[];
extern const u8 _binary_splash_bmp_end[];
extern const u8 _binary_test_ppm_start[];
extern const u8 _binary_test_ppm_end[];

#define DECL_BIN(n)                               \
    extern const u8 _binary_##n##_elf_start[];    \
    extern const u8 _binary_##n##_elf_end[];

DECL_BIN(ls)
DECL_BIN(cat)
DECL_BIN(echo)
DECL_BIN(img)
DECL_BIN(vid)
DECL_BIN(ltest)
DECL_BIN(about)
DECL_BIN(newfetch)
DECL_BIN(desktop)
DECL_BIN(kilo)
DECL_BIN(mkdir)
DECL_BIN(rmdir)
DECL_BIN(rm)
DECL_BIN(mv)
DECL_BIN(touch)
DECL_BIN(pwd)
DECL_BIN(clear)
DECL_BIN(uname)
DECL_BIN(hostname)
DECL_BIN(whoami)
DECL_BIN(id)
DECL_BIN(date)
DECL_BIN(uptime)
DECL_BIN(ps)
DECL_BIN(kill)
DECL_BIN(sleep)
DECL_BIN(free)
DECL_BIN(df)
DECL_BIN(wc)
DECL_BIN(head)
DECL_BIN(grep)
DECL_BIN(find)
DECL_BIN(sort)
DECL_BIN(newpkg)

/* nano is a command-name alias of the Kilo editor binary. */
extern const u8 _binary_nano_elf_start[];
extern const u8 _binary_nano_elf_end[];

/* Host-built GNU tools, packed by scripts/pack-gnu.sh into one `.new`
 * archive and staged at /tmp for the shell's startup install. */
extern const u8 _binary_gnu_coreutils_new_start[];
extern const u8 _binary_gnu_coreutils_new_end[];

/* Sample `.new` package embedded by the build (tools/newpkg): staged as
 * `sample.new` -> _binary_sample_new_{start,end}. */
extern const u8 _binary_sample_new_start[];
extern const u8 _binary_sample_new_end[];

/* Shell boot script: user/rootfs/nsh.rc -> _binary_nsh_rc_{start,end}. */
extern const u8 _binary_nsh_rc_start[];
extern const u8 _binary_nsh_rc_end[];

static const struct {
    const char *name;
    const u8 *start;
    const u8 *end;
} bin_table[] = {
#define BIN(n) { #n, _binary_##n##_elf_start, _binary_##n##_elf_end },
    BIN(ls)
    BIN(cat)
    BIN(echo)
    BIN(img)
    BIN(vid)
    BIN(ltest)
    BIN(about)
    BIN(newfetch)
    BIN(desktop)
    BIN(kilo)
    BIN(mkdir)
    BIN(rmdir)
    BIN(rm)
    BIN(mv)
    BIN(touch)
    BIN(pwd)
    BIN(clear)
    BIN(uname)
    BIN(hostname)
    BIN(whoami)
    BIN(id)
    BIN(date)
    BIN(uptime)
    BIN(ps)
    BIN(kill)
    BIN(sleep)
    BIN(free)
    BIN(df)
    BIN(wc)
    BIN(head)
    BIN(grep)
    BIN(find)
    BIN(sort)
    BIN(newpkg)
    { "nano", _binary_nano_elf_start, _binary_nano_elf_end },
};

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
    vfs_mkdir("/home/user");
    /* Conventional Linux-style hierarchy. Directories are created parent
     * first because this VFS intentionally does not synthesize missing ones. */
    static const char *system_dirs[] = {
        "/usr/bin", "/usr/sbin", "/usr/lib", "/usr/lib64", "/usr/include",
        "/usr/share", "/usr/share/doc", "/usr/share/doc/newos",
        "/usr/share/man", "/usr/share/misc", "/usr/share/licenses",
        "/usr/src", "/usr/local",
        "/usr/local/bin", "/usr/local/sbin", "/usr/local/lib",
        "/usr/local/share", "/usr/local/etc", "/etc/skel", "/etc/init.d",
        "/etc/opt", "/run/lock", "/var/cache", "/var/cache/newpkg",
        "/var/lib", "/var/lib/misc", "/var/log", "/var/opt", "/var/spool",
        "/var/tmp", "/srv", "/media", "/home/user/.config",
        "/home/user/.cache", "/home/user/.local", "/home/user/.local/share",
        "/home/user/Desktop", "/home/user/Documents",
        "/home/user/Downloads", "/home/user/Pictures",
        "/root/Desktop", "/root/Documents", "/root/Downloads",
        "/root/Pictures"
    };
    for (size_t i = 0; i < sizeof(system_dirs) / sizeof(system_dirs[0]); i++)
        vfs_mkdir(system_dirs[i]);
    /* Package database home (userspace newpkg registers installs here). */
    vfs_mkdir("/var/lib");
    vfs_mkdir("/var/lib/newpkg");

    /* Classic dotfiles. */
    static const char hostname[] = "newos\n";
    static const char passwd[] =
        "root:x:0:0:root:/root:/bin/sh\n"
        "user:x:1000:1000:user:/home/user:/bin/sh\n";
    static const char group[] =
        "root:x:0:\n"
        "users:x:100:\n"
        "user:x:1000:user\n";
    static const char shells[] = "/bin/sh\n";
    static const char issue[] = "NEWOS 0.3.0 \\n \\m (x86_64)\n\n";
    static const char profile[] =
        "# Per-user shell setup for NEWOS.\n"
        "# Built-in programs are in /bin; installed tools are in /usr/bin.\n";
    static const char motd[] =
        "\n"
        "  NEWOS 0.3.0 (x86_64) - nsh shell\n"
        "  Type 'help' for commands.\n"
        "\n";
    static const char os_release[] =
        "NAME=\"NEWOS\"\n"
        "PRETTY_NAME=\"NEWOS 0.3.0\"\n"
        "ID=newos\n"
        "VERSION_ID=\"0.3.0\"\n"
        "VERSION_CODENAME=\"gui\"\n"
        "HOME_URL=\"https://github.com/CRISTOP-bot/NEWOS\"\n";
    static const char distro_readme[] =
        "NEWOS\n"
        "=====\n"
        "Native x86_64 operating system.\n"
        "\n"
        "System commands: /bin\n"
        "Additional packages: /usr/bin\n"
        "Configuration: /etc\n"
        "Variable state: /var\n";
    static const char root_readme[] =
        "This is root's home. Be careful, you are the superuser.\n";
    static const char user_readme[] =
        "Welcome home. Try: ls /bin -- cat /etc/motd -- help\n";

    struct write_req {
        const char *path;
        const char *data;
    };
    static const struct write_req dotfiles[] = {
        { "/etc/hostname", hostname },
        { "/etc/passwd", passwd },
        { "/etc/group", group },
        { "/etc/shells", shells },
        { "/etc/issue", issue },
        { "/etc/motd", motd },
        { "/etc/os-release", os_release },
        { "/usr/share/doc/newos/README", distro_readme },
        { "/etc/skel/.profile", profile },
        { "/home/user/.profile", profile },
        { "/root/.profile", profile },
        { "/root/README", root_readme },
        { "/home/user/README", user_readme },
    };
    for (size_t i = 0; i < sizeof(dotfiles) / sizeof(dotfiles[0]); i++) {
        if (vfs_create(dotfiles[i].path) != 0)
            continue;
        struct vfs_file *f =
            vfs_open(dotfiles[i].path, VFS_O_READ | VFS_O_WRITE);
        if (f) {
            vfs_write(f, dotfiles[i].data, strlen(dotfiles[i].data));
            vfs_close(f);
        }
    }

    /* A welcome file so userspace has something to read. */
    vfs_create("/etc/version");
    struct vfs_file *f = vfs_open("/etc/version", VFS_O_READ | VFS_O_WRITE);
    if (f) {
        static const char banner[] = "NEWOS 0.2.0-pre-alpha\n";
        vfs_write(f, banner, sizeof(banner) - 1);
        vfs_close(f);
    }

    /* Test pictures for the img viewer (generated at build time) and the
     * shell boot script. Empty blobs are skipped, so an asset the build
     * never produced simply does not appear in the image. */
    {
        struct {
            const char *path;
            const u8 *start;
            const u8 *end;
        } files[] = {
            { "/etc/splash.bmp", _binary_splash_bmp_start,
              _binary_splash_bmp_end },
            { "/etc/test.ppm", _binary_test_ppm_start,
              _binary_test_ppm_end },
            { "/etc/nsh.rc", _binary_nsh_rc_start,
              _binary_nsh_rc_end },
        };
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            size_t len = (size_t)(files[i].end - files[i].start);
            struct vfs_file *pf;
            if (!len)
                continue;
            if (vfs_create(files[i].path) != 0)
                continue;
            pf = vfs_open(files[i].path, VFS_O_READ | VFS_O_WRITE);
            if (!pf)
                continue;
            if (vfs_write(pf, files[i].start, len) != (int)len)
                pr_warn("initramfs: short write %s\n", files[i].path);
            vfs_close(pf);
            pr_info("initramfs: installed %s (%zu bytes)\n", files[i].path,
                    len);
        }
    }

    /* /bin/hello (phase-2 test program) and /init (the interactive shell). */
    size_t hello_len = (size_t)(_binary_hello_elf_end - _binary_hello_elf_start);
    size_t init_len = (size_t)(_binary_init_elf_end - _binary_init_elf_start);
    if (hello_len) {
        struct vfs_inode *bin = vfs_lookup("/bin");
        if (bin)
            install_blob("/bin/hello", _binary_hello_elf_start, hello_len, bin);
        pr_info("initramfs: installed /bin/hello (%zu bytes)\n", hello_len);
    }
    if (init_len) {
        struct vfs_inode *root = vfs_root();
        struct vfs_inode *bin = vfs_lookup("/bin");
        if (root)
            install_blob("/init", _binary_init_elf_start, init_len, root);
        if (bin)
            install_blob("/bin/sh", _binary_init_elf_start, init_len, bin);
        pr_info("initramfs: installed /init + /bin/sh (%zu bytes)\n",
                init_len);
    }

    /* The /bin toolbox: every userland command. */
    {
        struct vfs_inode *bin = vfs_lookup("/bin");
        size_t installed = 0;
        if (bin) {
            for (size_t i = 0;
                 i < sizeof(bin_table) / sizeof(bin_table[0]); i++) {
                size_t len =
                    (size_t)(bin_table[i].end - bin_table[i].start);
                if (!len)
                    continue;
                char path[80];
                strcpy(path, "/bin/");
                strcat(path, bin_table[i].name);
                install_blob(path, bin_table[i].start, len, bin);
                installed++;
            }
        }
        pr_info("initramfs: installed %zu /bin tools\n", installed);
    }

    /* `.new` archives staged for live exercises:
     *   newpkg verify /tmp/hello-new-1.0.0-x86_64.new
     *   newpkg install /tmp/hello-new-1.0.0-x86_64.new
     *   newpkg install /tmp/gnu-coreutils.new
     * The first is built deterministically by tools/newpkg/newpkg-build.py,
     * the second by scripts/pack-gnu.sh (`make gnu && make gnu-pkg`). */
    {
        struct {
            const char *path;
            const u8 *start;
            const u8 *end;
        } pkgs[] = {
            { "/tmp/hello-new-1.0.0-x86_64.new",
              _binary_sample_new_start, _binary_sample_new_end },
            { "/tmp/gnu-coreutils.new",
              _binary_gnu_coreutils_new_start,
              _binary_gnu_coreutils_new_end },
        };
        for (size_t i = 0; i < sizeof(pkgs) / sizeof(pkgs[0]); i++) {
            size_t len = (size_t)(pkgs[i].end - pkgs[i].start);
            struct vfs_file *pf;
            if (!len)
                continue;
            if (vfs_create(pkgs[i].path) != 0) {
                pr_warn("initramfs: cannot stage %s\n", pkgs[i].path);
                continue;
            }
            pf = vfs_open(pkgs[i].path, VFS_O_READ | VFS_O_WRITE);
            if (!pf) {
                pr_warn("initramfs: cannot open %s\n", pkgs[i].path);
                continue;
            }
            if (vfs_write(pf, pkgs[i].start, len) != (int)len)
                pr_warn("initramfs: short write %s\n", pkgs[i].path);
            vfs_close(pf);
            pr_info("initramfs: staged %s (%zu bytes)\n", pkgs[i].path, len);
        }
    }

    pr_info("initramfs: built-in root populated\n");
}
