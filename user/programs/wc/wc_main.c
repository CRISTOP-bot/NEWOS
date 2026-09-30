#include "../../lib/nshlib.h"

static int count_fd(int fd, int show_l, int show_w, int show_c,
                    const char *tag, int multi)
{
    char buf[512];
    u64 lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    long n;

    for (;;) {
        n = sys_read(fd, buf, sizeof(buf));
        if (n < 0)
            return fail("wc", "read error");
        if (n == 0)
            break;
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n')
                lines++;
            if (c == ' ' || c == '\t' || c == '\n')
                in_word = 0;
            else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    if (show_l) {
        putu(lines);
        nputc(' ');
    }
    if (show_w) {
        putu(words);
        nputc(' ');
    }
    if (show_c) {
        putu(bytes);
        nputc(' ');
    }
    if (multi)
        nputs(tag);
    nputc('\n');
    return 0;
}

int main(int argc, char **argv)
{
    int l = 0, w = 0, c = 0, rc = 0, nfiles = 0, i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (int k = 1; argv[i][k]; k++) {
            if (argv[i][k] == 'l')
                l = 1;
            else if (argv[i][k] == 'w')
                w = 1;
            else if (argv[i][k] == 'c')
                c = 1;
            else
                return fail("wc", "unknown option");
        }
    }
    if (!l && !w && !c)
        l = w = c = 1;
    for (int j = i; j < argc; j++)
        nfiles++;
    if (!nfiles)
        return count_fd(0, l, w, c, "", 0);
    for (; i < argc; i++) {
        long fd = sys_open(argv[i], O_READ);
        if (fd < 0) {
            fputf(1, "wc: %s: no such file\n", argv[i]);
            rc = 1;
            continue;
        }
        if (count_fd((int)fd, l, w, c, argv[i], nfiles > 1))
            rc = 1;
        sys_close((int)fd);
    }
    return rc;
}
