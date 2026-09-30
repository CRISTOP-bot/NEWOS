#include "../../lib/nshlib.h"

static int show(void)
{
    char buf[128];
    long n, fd = sys_open("/etc/hostname", O_READ);

    if (fd < 0)
        return fail("hostname", "cannot read /etc/hostname");
    n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return fail("hostname", "cannot read /etc/hostname");
    buf[n] = '\0';
    nputs(buf);
    if (buf[n - 1] != '\n')
        nputc('\n');
    return 0;
}

int main(int argc, char **argv)
{
    long fd;

    if (argc == 1)
        return show();
    if (argc > 2)
        return fail("hostname", "too many arguments");
    /* Replace /etc/hostname (no truncate syscall: rewrite the file). */
    sys_unlink("/etc/hostname");
    fd = sys_open("/etc/hostname", O_WRITE | O_CREATE);
    if (fd < 0)
        return fail("hostname", "cannot write /etc/hostname");
    sys_write((int)fd, argv[1], nstrlen(argv[1]));
    sys_write((int)fd, "\n", 1);
    sys_close((int)fd);
    return 0;
}
