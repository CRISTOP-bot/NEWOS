#include "../../lib/nshlib.h"

#define MAX_LINES 256
#define LINE_LEN  128

static char lines[MAX_LINES][LINE_LEN];
static int nlines;

static int read_all(int fd)
{
    char cur[LINE_LEN];
    u64 n = 0;

    for (;;) {
        char c;
        long r = sys_read(fd, &c, 1);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (n) {
                cur[n] = '\0';
                if (nlines < MAX_LINES)
                    nstrcpy(lines[nlines++], cur);
            }
            break;
        }
        if (c == '\n') {
            cur[n] = '\0';
            if (nlines < MAX_LINES)
                nstrcpy(lines[nlines++], cur);
            n = 0;
            continue;
        }
        if (n + 1 < sizeof(cur))
            cur[n++] = c;
    }
    return 0;
}

int main(int argc, char **argv)
{
    nlines = 0;
    if (argc == 1) {
        if (read_all(0) != 0)
            return fail("sort", "read error");
    }
    for (int i = 1; i < argc; i++) {
        long fd = sys_open(argv[i], O_READ);
        if (fd < 0) {
            fputf(1, "sort: %s: no such file\n", argv[i]);
            return 1;
        }
        if (read_all((int)fd) != 0) {
            fputf(1, "sort: %s: read error\n", argv[i]);
            sys_close((int)fd);
            return 1;
        }
        sys_close((int)fd);
    }
    for (int i = 1; i < nlines; i++) {
        char t[LINE_LEN];
        int j = i - 1;
        nstrcpy(t, lines[i]);
        while (j >= 0 && nstrcmp(lines[j], t) > 0) {
            nstrcpy(lines[j + 1], lines[j]);
            j--;
        }
        nstrcpy(lines[j + 1], t);
    }
    for (int i = 0; i < nlines; i++) {
        nputs(lines[i]);
        nputc('\n');
    }
    return 0;
}
