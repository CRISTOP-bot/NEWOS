#include "../../lib/nshlib.h"

/* Prints uid/gid parsed from the first /etc/passwd entry
 * (name:x:uid:gid:...). Single-user system: normally uid=0(root). */

static long parse_num(const char *s, long *pos, long max)
{
    long v = 0;
    while (*pos < max && s[*pos] >= '0' && s[*pos] <= '9') {
        v = v * 10 + (s[*pos] - '0');
        (*pos)++;
    }
    return v;
}

int main(int argc, char **argv)
{
    char buf[128], name[32];
    long n, fd = sys_open("/etc/passwd", O_READ);
    long i = 0, ni = 0;
    long uid, gid;

    (void)argc;
    (void)argv;
    if (fd < 0)
        return fail("id", "cannot read /etc/passwd");
    n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return fail("id", "cannot read /etc/passwd");
    buf[n] = '\0';

    while (i < n && buf[i] != ':' && buf[i] != '\n' && ni < 31)
        name[ni++] = buf[i++];
    name[ni] = '\0';
    if (ni == 0 || i >= n || buf[i] != ':')
        return fail("id", "bad passwd entry");
    i++;                        /* skip password field */
    while (i < n && buf[i] != ':' && buf[i] != '\n')
        i++;
    if (i >= n || buf[i] != ':')
        return fail("id", "bad passwd entry");
    i++;
    uid = parse_num(buf, &i, n);
    if (i >= n || buf[i] != ':')
        return fail("id", "bad passwd entry");
    i++;
    gid = parse_num(buf, &i, n);

    putf("uid=%d(%s) gid=%d(%s) groups=%d(%s)\n", uid, name, gid, name,
         gid, name);
    return 0;
}
