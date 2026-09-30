#include "../../lib/nshlib.h"

static int dump(int fd, const char *prog)
{
    char buf[512];
    long n;

    for (;;) {
        n = sys_read(fd, buf, sizeof(buf));
        if (n < 0)
            return fail(prog, "read error");
        if (n == 0)
            return 0;
        sys_write(1, buf, (u64)n);
    }
}

int main(int argc, char **argv)
{
    int rc = 0;

    if (argc == 1)
        return dump(0, "cat");
    for (int i = 1; i < argc; i++) {
        long fd = sys_open(argv[i], O_READ);
        if (fd < 0) {
            fputf(1, "cat: %s: no such file\n", argv[i]);
            rc = 1;
            continue;
        }
        if (dump((int)fd, "cat"))
            rc = 1;
        sys_close((int)fd);
    }
    return rc;
}
