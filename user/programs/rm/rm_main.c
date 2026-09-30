#include "../../lib/nshlib.h"

/* rm [-r] path... Without -r, directories are refused like real rm;
 * with -r they are deleted recursively. */

static int rm_one(const char *path, int recursive);

static int rm_children(const char *path, int recursive)
{
    static struct nsh_dirent dents[64];
    long n = sys_readdir(path, dents, sizeof(dents));
    int rc = 0;
    char child[256];

    if (n < 0)
        return 1;
    for (long i = 0; i < n; i++) {
        u64 a = nstrlen(path), b = nstrlen(dents[i].name);
        if (a + 1 + b >= sizeof(child))
            return 1;
        nstrcpy(child, path);
        if (a == 0 || child[a - 1] != '/')
            nstrcat(child, "/");
        nstrcat(child, dents[i].name);
        if (rm_one(child, recursive))
            rc = 1;
    }
    return rc;
}

static int rm_one(const char *path, int recursive)
{
    static struct nsh_dirent probe[1];
    int is_dir = sys_readdir(path, probe, sizeof(probe)) >= 0;

    if (is_dir) {
        if (!recursive) {
            fputf(1, "rm: cannot remove '%s': is a directory\n", path);
            return 1;
        }
        if (rm_children(path, recursive))
            return 1;
    }
    if (sys_unlink(path) != 0) {
        fputf(1, "rm: cannot remove '%s'\n", path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int recursive = 0, rc = 0, i = 1;

    if (argc > 1 &&
        (nstrcmp(argv[1], "-r") == 0 || nstrcmp(argv[1], "-R") == 0 ||
         nstrcmp(argv[1], "-rf") == 0)) {
        recursive = 1;
        i = 2;
    }
    if (i >= argc)
        return fail("rm", "missing operand");
    for (; i < argc; i++) {
        if (rm_one(argv[i], recursive))
            rc = 1;
    }
    return rc;
}
