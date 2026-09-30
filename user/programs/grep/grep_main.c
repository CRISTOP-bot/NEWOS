#include "../../lib/nshlib.h"

static int match_at(const char *line, const char *pat, int icase)
{
    for (; *line; line++) {
        const char *l = line, *p = pat;
        for (; *p; l++, p++) {
            char a = *l, b = *p;
            if (icase) {
                if (a >= 'A' && a <= 'Z')
                    a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = (char)(b + 32);
            }
            if (a != b)
                break;
        }
        if (!*p)
            return 1;
    }
    return 0;
}

/* Read one line (NUL-terminated, without newline). Returns length, 0 at
 * EOF (empty), -1 on error. Overlong lines are truncated. */
static long read_line(int fd, char *buf, u64 cap)
{
    u64 n = 0;
    while (n + 1 < cap) {
        char c;
        long r = sys_read(fd, &c, 1);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        if (c == '\n')
            break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    /* Swallow the rest of an overlong line. */
    if (n + 1 == cap) {
        char c;
        long r;
        do {
            r = sys_read(fd, &c, 1);
        } while (r > 0 && c != '\n');
    }
    return (long)n;
}

static int grep_fd(int fd, const char *pat, int icase, int number,
                   const char *tag, int show_tag)
{
    static char line[512];
    long ln = 0, rc = 1;

    for (;;) {
        long n = read_line(fd, line, sizeof(line));
        if (n < 0)
            return fail("grep", "read error");
        if (n == 0)
            break;
        ln++;
        if (match_at(line, pat, icase)) {
            rc = 0;
            if (show_tag)
                putf("%s:", tag);
            if (number)
                putf("%u:", (u64)ln);
            nputs(line);
            nputc('\n');
        }
    }
    return rc;
}

int main(int argc, char **argv)
{
    int icase = 0, number = 0, rc = 1, i = 1, nfiles = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (int k = 1; argv[i][k]; k++) {
            if (argv[i][k] == 'i')
                icase = 1;
            else if (argv[i][k] == 'n')
                number = 1;
            else
                return fail("grep", "unknown option");
        }
    }
    if (i >= argc)
        return fail("grep", "missing pattern");
    const char *pat = argv[i++];
    for (int j = i; j < argc; j++)
        nfiles++;
    if (!nfiles)
        return grep_fd(0, pat, icase, number, "", 0);
    for (; i < argc; i++) {
        long fd = sys_open(argv[i], O_READ);
        if (fd < 0) {
            fputf(1, "grep: %s: no such file\n", argv[i]);
            continue;
        }
        if (grep_fd((int)fd, pat, icase, number, argv[i], nfiles > 1) == 0)
            rc = 0;
        sys_close((int)fd);
    }
    return rc;
}
