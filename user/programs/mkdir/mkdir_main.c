#include "../../lib/nshlib.h"

/* mkdir [-p] dir... (-p builds missing parents like the real one). */
static int mkdir_one(const char *path)
{
    if (sys_mkdir(path) == 0)
        return 0;
    fputf(1, "mkdir: cannot create '%s'\n", path);
    return 1;
}

static int mkdir_p(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (sys_mkdir(path) != 0) {
                static struct nsh_dirent d[1];
                if (sys_readdir(path, d, sizeof(d)) < 0) {
                    fputf(1, "mkdir: cannot create '%s'\n", path);
                    *p = '/';
                    return 1;
                }
            }
            *p = '/';
        }
    }
    return mkdir_one(path);
}

int main(int argc, char **argv)
{
    int p = 0, rc = 0, i = 1;

    if (argc > 1 && nstrcmp(argv[1], "-p") == 0) {
        p = 1;
        i = 2;
    }
    if (i >= argc)
        return fail("mkdir", "missing operand");
    for (; i < argc; i++) {
        if (p) {
            char buf[256];
            u64 n = nstrlen(argv[i]);
            if (n >= sizeof(buf))
                return fail("mkdir", "path too long");
            nstrcpy(buf, argv[i]);
            if (mkdir_p(buf))
                rc = 1;
        } else if (mkdir_one(argv[i])) {
            rc = 1;
        }
    }
    return rc;
}
