#include "../../lib/nshlib.h"

static void perms(u32 mode)
{
    nputc((mode & MODE_DIR) ? 'd' : '-');
    for (int s = 6; s >= 0; s -= 3) {
        u32 b = (mode >> s) & 7;
        nputc((b & 4) ? 'r' : '-');
        nputc((b & 2) ? 'w' : '-');
        nputc((b & 1) ? 'x' : '-');
    }
}

static int list_one(const char *path, int all, int longfmt)
{
    static struct nsh_dirent dents[64];
    long n = sys_readdir(path, dents, sizeof(dents));
    if (n < 0) {
        fputf(1, "ls: %s: no such directory\n", path);
        return 1;
    }

    /* Sort by name (insertion sort, few entries). */
    for (long i = 1; i < n; i++) {
        struct nsh_dirent t = dents[i];
        long j = i - 1;
        while (j >= 0 && nstrcmp(dents[j].name, t.name) > 0) {
            dents[j + 1] = dents[j];
            j--;
        }
        dents[j + 1] = t;
    }

    for (long i = 0; i < n; i++) {
        if (!all && dents[i].name[0] == '.')
            continue;
        if (longfmt) {
            perms(dents[i].mode);
            putf(" %8u %s", dents[i].size, dents[i].name);
            if (dents[i].mode & MODE_DIR)
                nputc('/');
            nputc('\n');
        } else {
            nputs(dents[i].name);
            if (dents[i].mode & MODE_DIR)
                nputc('/');
            nputc('\n');
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int all = 0, longfmt = 0, rc = 0, npaths = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (int k = 1; argv[i][k]; k++) {
                if (argv[i][k] == 'a')
                    all = 1;
                else if (argv[i][k] == 'l')
                    longfmt = 1;
                else
                    return fail("ls", "unknown option");
            }
        } else {
            npaths++;
        }
    }
    if (!npaths)
        return list_one(".", all, longfmt);
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        if (npaths > 1)
            putf("%s:\n", argv[i]);
        if (list_one(argv[i], all, longfmt))
            rc = 1;
    }
    return rc;
}
