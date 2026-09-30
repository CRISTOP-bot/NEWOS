#include <fs/vfs.h>
#include <fs/tmpfs.h>
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

struct vfs_inode *vfs_lookup_cwd(const char *cwd, const char *path)
{
    static char full[512];

    if (!path || !*path)
        return NULL;
    if (path[0] == '/')
        return resolve(path);
    if (!cwd || cwd[0] != '/')
        cwd = "/";

    size_t cl = strlen(cwd);
    size_t pl = strlen(path);
    if (cl + 1 + pl >= sizeof(full))
        return NULL;
    memcpy(full, cwd, cl);
    size_t n = cl;
    if (n == 0 || full[n - 1] != '/')
        full[n++] = '/';
    memcpy(full + n, path, pl + 1);
    return resolve(full);
}

/* Split `path` into its parent directory and final component.
 * Returns 0 with the parent inode and the name (into `copy`) set. */
static int split_parent(const char *path, char *copy, size_t copy_size,
                        struct vfs_inode **parent_out, char **name_out)
{
    if (!path || strlen(path) >= copy_size)
        return -1;
    strcpy(copy, path);

    char *name = copy + strlen(copy);
    while (name > copy && *(name - 1) == '/' && (size_t)(name - copy) > 1)
        *(--name) = '\0';              /* strip trailing slashes */
    while (name > copy && *(name - 1) != '/')
        name--;

    struct vfs_inode *parent;
    if (name == copy) {
        parent = g_root;
        while (*name == '/')
            name++;
    } else {
        /* The walk above leaves `name` at the first basename char, so
         * the separator is the byte just before it (name > copy here). */
        *(name - 1) = '\0';
        parent = (*copy == '\0') ? g_root : resolve(copy);
    }

    if (!parent || !(parent->mode & VFS_MODE_DIR) || *name == '\0')
        return -1;
    *parent_out = parent;
    *name_out = name;
    return 0;
}

int vfs_unlink(const char *path)
{
    static char copy[512];
    struct vfs_inode *parent;
    char *name;

    if (split_parent(path, copy, sizeof(copy), &parent, &name) != 0)
        return -1;

    struct vfs_inode *in = find_child(parent, name);
    if (!in)
        return -1;
    if ((in->mode & VFS_MODE_TYPE_MASK) == VFS_MODE_DIR &&
        !list_is_empty(&in->children))
        return -1;                     /* refuse non-empty directories */

    list_remove(&in->chain);
    if (parent->size > 0)
        parent->size--;
    if (in->ops && in->ops->destroy)
        in->ops->destroy(in);
    kfree(in);
    return 0;
}

/* Only the synthetic trees keep their directory state in the inode list,
 * so a rename is limited to directories created by the VFS core itself. */
static int in_memory_dir(const struct vfs_inode *dir)
{
    return dir && (dir->mode & VFS_MODE_DIR) && dir->ops == &default_ops;
}

static int is_ancestor(struct vfs_inode *anc, struct vfs_inode *in)
{
    while (in) {
        if (in == anc)
            return 1;
        in = in->parent;
    }
    return 0;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    static char ocopy[512], ncopy[512];
    struct vfs_inode *oparent, *nparent, *src, *dst;
    char *oname, *nname;
    u32 src_type;

    if (split_parent(oldpath, ocopy, sizeof(ocopy), &oparent, &oname) != 0)
        return -1;
    if (split_parent(newpath, ncopy, sizeof(ncopy), &nparent, &nname) != 0)
        return -1;
    if (!in_memory_dir(oparent) || !in_memory_dir(nparent))
        return -1;

    src = find_child(oparent, oname);
    if (!src)
        return -1;
    if (strlen(nname) >= sizeof(src->name))
        return -1;

    dst = find_child(nparent, nname);
    if (dst == src)
        return 0;                      /* rename(a,a) is a no-op */

    src_type = src->mode & VFS_MODE_TYPE_MASK;
    if (src_type == VFS_MODE_DIR && is_ancestor(src, nparent))
        return -1;                     /* would orphan the destination */

    if (dst) {
        u32 dst_type = dst->mode & VFS_MODE_TYPE_MASK;
        if (src_type == VFS_MODE_DIR) {
            if (dst_type != VFS_MODE_DIR)
                return -1;             /* not a directory */
            if (!list_is_empty(&dst->children))
                return -1;             /* directory not empty */
        } else if (dst_type == VFS_MODE_DIR) {
            return -1;                 /* is a directory */
        }
        if (vfs_unlink(newpath) != 0)
            return -1;
    }

    list_remove(&src->chain);
    if (oparent != nparent) {
        if (oparent->size > 0)
            oparent->size--;
        nparent->size++;
    }
    memset(src->name, 0, sizeof(src->name));
    strcpy(src->name, nname);
    src->parent = nparent;
    list_push_back(&nparent->children, &src->chain);
    return 0;
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

    /* Regular files live on tmpfs so they support read/write through the
     * same ops as the rest of the tree (a bare VFS node would refuse I/O). */
    struct vfs_inode *in = tmpfs_create_node(parent, name,
                                             VFS_MODE_REG | 0644);
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
    if (!in) {
        /* O_CREATE: materialize a regular file, then resolve again. */
        if (!(flags & VFS_O_CREATE))
            return NULL;
        if (vfs_create(path) != 0)
            return NULL;
        in = resolve(path);
        if (!in)
            return NULL;
    }
    return vfs_open_inode(in, flags);
}

int vfs_read(struct vfs_file *f, void *buf, size_t len)
{
    if (!f || !f->inode->ops || !f->inode->ops->read)
        return -1;
    size_t got = 0;
    int rc = f->inode->ops->read(f, buf, len, &got);
    if (rc != 0)
        return -1;
    f->offset += got;
    return (int)got;
}

int vfs_write(struct vfs_file *f, const void *buf, size_t len)
{
    if (!f || !f->inode->ops || !f->inode->ops->write)
        return -1;
    size_t wrote = 0;
    int rc = f->inode->ops->write(f, buf, len, &wrote);
    if (rc != 0)
        return -1;
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