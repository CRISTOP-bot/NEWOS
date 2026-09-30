#include "../../lib/nshlib.h"

/* mv SRC... DST
 *
 * Everything the VFS can rename lives in the in-memory tree, so a move is
 * just a re-link: no copy-and-unlink fallback is needed here. A trailing
 * directory argument behaves like real mv (move *into* it by basename).
 */

static const char *base_name(const char *p)
{
    const char *last = p;
    for (const char *s = p; *s; s++)
        if (*s == '/')
            last = s + 1;
    while (*last == '/' && *(last + 1))
        last++;
    return last;
}

static int is_dir(const char *path)
{
    static struct nsh_dirent probe[1];
    return sys_readdir(path, probe, sizeof(probe)) >= 0;
}

static int join(char *out, u64 cap, const char *dir, const char *name)
{
    u64 a = nstrlen(dir), b = nstrlen(name);
    if (a + 1 + b + 1 > cap)
        return -1;
    nstrcpy(out, dir);
    if (a == 0 || out[a - 1] != '/')
        nstrcat(out, "/");
    nstrcat(out, name);
    return 0;
}

static int move_into(const char *src, const char *dst_dir)
{
    char dst[256];

    if (join(dst, sizeof(dst), dst_dir, base_name(src)) != 0) {
        fputf(1, "mv: target name too long: '%s'\n", src);
        return 1;
    }
    if (sys_rename(src, dst) != 0) {
        fputf(1, "mv: cannot move '%s' to '%s'\n", src, dst);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 0;

    if (argc < 3)
        return fail("mv", "missing operand");

    const char *dst = argv[argc - 1];
    int many = (argc - 2 > 1);

    if (many || is_dir(dst)) {
        if (!is_dir(dst)) {
            fputf(1, "mv: target '%s' is not a directory\n", dst);
            return 1;
        }
        for (int i = 1; i < argc - 1; i++) {
            if (nstrcmp(argv[i], dst) == 0) {
                fputf(1, "mv: cannot move '%s' into itself\n", argv[i]);
                rc = 1;
                continue;
            }
            if (move_into(argv[i], dst))
                rc = 1;
        }
        return rc;
    }

    if (sys_rename(argv[1], dst) != 0) {
        fputf(1, "mv: cannot move '%s' to '%s'\n", argv[1], dst);
        return 1;
    }
    return 0;
}
