#include <fs/vfs.h>
#include <mm/mm_heap.h>
#include <core/core_panic.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* Default no-op operations for plain VFS nodes. */

static int missing_read(struct vfs_file *f, void *buf, size_t len, size_t *got)
{
    (void)f; (void)buf; (void)len; *got = 0;
    return -1;
}

static int missing_write(struct vfs_file *f, const void *buf, size_t len,
                         size_t *wrote)
{
    (void)f; (void)buf; (void)len; *wrote = 0;
    return -1;
}

static int generic_readdir(struct vfs_inode *dir, void *buf, size_t len,
                           size_t *got)
{
    (void)dir; (void)buf; (void)len; *got = 0;
    return -1;
}

static int generic_truncate(struct vfs_inode *inode, size_t size)
{
    inode->size = size;
    return 0;
}

static struct inode_ops default_ops = {
    .read     = missing_read,
    .write    = missing_write,
    .readdir  = generic_readdir,
    .truncate = generic_truncate,
};

static struct vfs_inode *g_root;

static struct vfs_inode *find_child(struct vfs_inode *dir, const char *name)
{
    struct list_node *node;
    LIST_FOR_EACH(node, &dir->children) {
        struct vfs_inode *in = LIST_NODE_ENTRY(node, struct vfs_inode, chain);
        if (strcmp(in->name, name) == 0)
            return in;
    }
    return NULL;
}

static struct vfs_inode *resolve(const char *path)
{
    struct vfs_inode *in = g_root;

    if (!path || *path == '\0')
        return in;
    if (path[0] == '/')
        path++;

    char component[128];
    while (*path) {
        const char *sep = strchr(path, '/');
        size_t len = sep ? (size_t)(sep - path) : strlen(path);
        if (len >= sizeof(component))
            return NULL;
        memcpy(component, path, len);
        component[len] = '\0';

        if (len == 0) {
            /* double slash: skip */
        } else if (strcmp(component, ".") == 0) {
            /* stay */
        } else if (strcmp(component, "..") == 0) {
            if (in->parent)
                in = in->parent;
        } else {
            in = find_child(in, component);
            if (!in)
                return NULL;
        }

        if (!sep)
            break;
        path = sep + 1;
    }

    return in;
}

struct vfs_inode *vfs_root(void)
{
    return g_root;
}

void vfs_init(void)
{
    g_root = vfs_alloc_inode(NULL, "", VFS_MODE_DIR | 0755, &default_ops, NULL);
    if (!g_root)
        panic("vfs: failed to allocate root inode");
    pr_info("VFS: root mount ready (inode %llu)\n", g_root->ino);
}

struct vfs_inode *vfs_lookup(const char *path)
{
    return resolve(path);
}

int vfs_mkdir(const char *path)
{
    /* Find parent directory and final component. */
    char copy[512];
    if (!path || strlen(path) >= sizeof(copy))
        return -1;
    strcpy(copy, path);

    char *name = copy + strlen(copy);
    while (name > copy && *(--name) != '/')
        ;

    struct vfs_inode *parent;
    char parent_path[512];
    if (name == copy) {
        /* No '/' separator (or only a leading one): strip any leading
         * slashes and create directly under the root. */
        parent = g_root;
        while (*name == '/')
            name++;
    } else {
        *name = '\0';
        strcpy(parent_path, copy);
        parent = resolve(parent_path);
        name++;
        while (*name == '/')
            name++;
    }

    if (!parent || !(parent->mode & VFS_MODE_DIR))
        return -1;

    if (find_child(parent, name))
        return -1;   /* exists */

    struct vfs_inode *dir = vfs_alloc_inode(
        parent, name, VFS_MODE_DIR | 0755, &default_ops, NULL);
    return dir ? 0 : -1;
}

int vfs_create(const char *path)
{
    char copy[512];
    if (!path || strlen(path) >= sizeof(copy))
        return -1;
    strcpy(copy, path);

    char *name = copy + strlen(copy);
    while (name > copy && *(--name) != '/')
        ;

    struct vfs_inode *parent;
    if (name == copy) {
        /* No '/' separator (or only a leading one) */
        parent = g_root;
        while (*name == '/')
            name++;
    } else {
        *name = '\0';
        parent = resolve(copy);
        name++;
        while (*name == '/')
            name++;
    }

    if (!parent || !(parent->mode & VFS_MODE_DIR))
        return -1;

    if (find_child(parent, name))
        return 0;   /* already exists: treat as success */

    struct vfs_inode *in = vfs_alloc_inode(
        parent, name, VFS_MODE_REG | 0644, &default_ops, NULL);
    return in ? 0 : -1;
}

struct vfs_file *vfs_open_inode(struct vfs_inode *inode, int flags)
{
    if (!inode)
        return NULL;

    struct vfs_file *f = kzalloc(sizeof(*f));
    if (!f)
        return NULL;

    f->inode = inode;
    f->offset = 0;
    f->flags = flags;
    return f;
}

struct vfs_file *vfs_open(const char *path, int flags)
{
    struct vfs_inode *in = resolve(path);
    if (!in)
        return NULL;
    return vfs_open_inode(in, flags);
}

int vfs_read(struct vfs_file *f, void *buf, size_t len)
{
    if (!f || !f->inode->ops || !f->inode->ops->read)
        return -1;
    size_t got = 0;
    int rc = f->inode->ops->read(f, buf, len, &got);
    if (rc == 0)
        f->offset += got;
    return (int)got;
}

int vfs_write(struct vfs_file *f, const void *buf, size_t len)
{
    if (!f || !f->inode->ops || !f->inode->ops->write)
        return -1;
    size_t wrote = 0;
    int rc = f->inode->ops->write(f, buf, len, &wrote);
    if (rc == 0)
        f->offset += wrote;
    return (int)wrote;
}

int vfs_close(struct vfs_file *f)
{
    if (!f)
        return -1;
    kfree(f);
    return 0;
}

int vfs_readdir(struct vfs_inode *dir, char **names, size_t max, size_t *n)
{
    if (!dir || !(dir->mode & VFS_MODE_DIR))
        return -1;

    /* Optional per-fs readdir is invoked first. */
    if (dir->ops->readdir) {
        char buffer[1024];
        size_t got = 0;
        int rc = dir->ops->readdir(dir, buffer, sizeof(buffer), &got);
        if (rc == 0 && got) {
            *n = got;
            return 0;
        }
    }

    /* Fallback: walk the in-memory children list. */
    size_t count = 0;
    struct list_node *node;
    LIST_FOR_EACH(node, &dir->children) {
        if (count >= max)
            break;
        struct vfs_inode *in = LIST_NODE_ENTRY(node, struct vfs_inode, chain);
        names[count] = in->name;
        count++;
    }

    *n = count;
    return 0;
}

void vfs_dump_tree(void)
{
    struct vfs_inode *dir = g_root;

    char names[64][64];
    size_t n = 0;
    struct list_node *node;
    LIST_FOR_EACH(node, &dir->children) {
        struct vfs_inode *in = LIST_NODE_ENTRY(node, struct vfs_inode, chain);
        (void)in;
        if (n < 64)
            names[n++][0] = '\0';
    }
    (void)names;

    printk("VFS: / (root, %zu children)\n", n);
}