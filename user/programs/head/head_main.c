#include "../../lib/nshlib.h"

static long head_fd(int fd, long lines)
{
    char buf[256];
    long left = lines, got = 0;

    while (left > 0) {
        long n = sys_read(fd, buf, 1);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        sys_write(1, buf, 1);
        got++;
        if (buf[0] == '\n')
            left--;
    }
    return got;
}

int main(int argc, char **argv)
{
    long lines = 10;
    int rc = 0, i = 1, nfiles = 0;

    if (argc > 2 && nstrcmp(argv[1], "-n") == 0) {
        lines = (long)natoi(argv[2]);
        if (lines < 0)
            return fail("head", "invalid line count");
        i = 3;
    }
    for (int j = i; j < argc; j++)
        nfiles++;
    if (!nfiles)
        return head_fd(0, lines) < 0 ? fail("head", "read error") : 0;
    for (; i < argc; i++) {
        long fd = sys_open(argv[i], O_READ);
        if (fd < 0) {
            fputf(1, "head: %s: no such file\n", argv[i]);
            rc = 1;
            continue;
        }
        if (nfiles > 1)
            putf("==> %s <==\n", argv[i]);
        if (head_fd((int)fd, lines) < 0) {
            fputf(1, "head: %s: read error\n", argv[i]);
            rc = 1;
        }
        sys_close((int)fd);
    }
    return rc;
}
